#include "landmarks.hpp"
#include "perception.hpp"
#include "prompt_labels.hpp"
#include "mask_match.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <span>
#include <stdexcept>
#include <vector>

namespace hive::game {
namespace {
using detail::reference;
using detail::similarity;
std::vector<cv::Rect> word_boxes(const cv::Mat& line,double gap=.25) {
    std::vector<cv::Rect> words;
    for (int x=0;x<line.cols;) {
        if (!cv::countNonZero(line.col(x))) { ++x; continue; }
        const int left=x++;
        int right=x;
        while (x<line.cols && x-right<line.rows*gap) {
            if (cv::countNonZero(line.col(x))) right=x+1;
            ++x;
        }
        words.push_back(cv::boundingRect(line.colRange(left,right))+cv::Point(left,0));
    }
    return words;
}
float words_similarity(const cv::Mat& line,const cv::Mat& pattern,unsigned prefix) {
    const auto expected=word_boxes(pattern,.4);
    if (expected.size()<=prefix) return 0;
    float best=0;
    // Rasterization changes the apparent gaps between letters at small UI scales.
    for (double gap:{.25,.4}) {
        const auto words=word_boxes(line,gap);
        if (words.size()!=expected.size()) continue;
        float total=0;
        for (std::size_t i=0;i<expected.size();++i) {
            const auto score=similarity(line(words[i]),pattern(expected[i]));
            if (score<(i<prefix?.6f:.75f)) { total=0; break; }
            if (i>=prefix) total+=score;
        }
        best=std::max(best,total/(expected.size()-prefix));
    }
    return best;
}
float phrase_similarity(const cv::Mat& ink,const cv::Mat& pattern) {
    // Keep word boundaries, but allow the rest of the caption to wrap independently.
    float best=0;
    for (int y=0;y<ink.rows;) {
        if (!cv::countNonZero(ink.row(y))) { ++y; continue; }
        const int top=y++;
        while (y<ink.rows && cv::countNonZero(ink.row(y))) ++y;
        const auto line=ink(cv::Rect(0,top,ink.cols,y-top));
        if (line.rows<8) continue;
        const auto words=word_boxes(line);
        for (std::size_t first=0;first<words.size();++first) for (std::size_t last=first;last<words.size();++last) {
            const auto phrase=line.colRange(words[first].x,words[last].x+words[last].width);
            best=std::max(best,similarity(phrase,pattern));
            if (best>=.94f) return best;
        }
    }
    return best;
}
bool interact_key(const Frame& frame,const cv::Rect& box) {
    const cv::Rect inner(box.x+box.width/5,box.y+box.height/10,box.width*3/5,box.height*4/5);
    cv::Mat ink(inner.height,inner.width,CV_8UC1,cv::Scalar(0));
    for (int y=0;y<inner.height;++y) for (int x=0;x<inner.width;++x) {
        const auto i=(std::size_t(inner.y+y)*frame.width+inner.x+x)*4;
        const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
        if (std::max({r,g,b})<140 && std::max({r,g,b})-std::min({r,g,b})<35) ink.at<unsigned char>(y,x)=255;
    }
    const auto bounds=cv::boundingRect(ink);
    if (bounds.height<box.height*.4 || bounds.width<box.width*.15) return false;
    static const auto pattern=reference(detail::interact_key_width,detail::interact_key_height,detail::interact_key);
    return similarity(ink(bounds),pattern)>=.85f;
}
bool banner_color(const Frame& frame,int x,int y) {
    const auto i=(std::size_t(y)*frame.width+x)*4;
    const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
    return (b>=100 && b>r*1.3 && b>g*1.15) || (r>=100 && r>g*1.3 && r>b*1.15);
}
struct PromptRegion { Rect bounds; bool key; };
std::optional<PromptRegion> locate_prompt(const Frame& frame,bool captions) {
    if (!frame.valid()) return {};
    cv::Mat light(frame.height/4,frame.width,CV_8UC1,cv::Scalar(0));
    for (int y=0;y<light.rows;++y) for (int x=frame.width/5;x<frame.width*4/5;++x) {
        const auto i=(std::size_t(y)*frame.width+x)*4;
        const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
        if (std::min({r,g,b})>=170 && std::max({r,g,b})-std::min({r,g,b})<65) light.at<unsigned char>(y,x)=255;
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(light,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    std::optional<PromptRegion> found;
    for (const auto& contour:contours) {
        const auto key=cv::boundingRect(contour);
        if (cv::contourArea(contour)<key.area()*.7) continue;
        if (key.height<frame.height*.018 || key.height>frame.height*.13 || key.width<key.height*.75 || key.width>key.height*1.3
            || !interact_key(frame,key)) continue;
        const int inset=std::max(2,key.height/12);
        const int left=key.x+key.width+inset,y=key.y,right=frame.width-key.x+inset,height=key.height;
        if (y+height>=frame.height || right>=frame.width || right-left<height*1.5 || right-left>height*18) continue;
        unsigned colored=0;
        for (double dy:{.08,.92}) for (double dx:{.08,.5,.92})
            colored+=banner_color(frame,left+int((right-left)*dx),y+int(height*dy));
        if (colored<4) continue;
        if (found) return {};
        found=PromptRegion{{double(left)/frame.width,double(y)/frame.height,double(right-left)/frame.width,double(height)/frame.height},true};
    }
    if (found || !captions) return found;
    for (int y=0;y<frame.height/6;++y) {
        std::vector<std::pair<int,int>> runs;
        const auto* row=light.ptr<unsigned char>(y);
        for (int x=frame.width/5;x<frame.width*4/5;) {
            if (!row[x]) {++x;continue;}
            const int begin=x++;
            while (x<frame.width*4/5 && row[x]) ++x;
            if (x-begin>=frame.height*.12 && x-begin<=frame.height*.6) runs.emplace_back(begin,x);
        }
        for (std::size_t i=1;i<runs.size();++i) {
            const auto [a,b]=runs[i-1];const auto [c,d]=runs[i];
            const double bar_width=((b-a)+(d-c))/2.,bar_height=bar_width/8.2;
            if (std::abs(c-frame.width/2.)>bar_height*.5 || c-b>bar_height*.75
                || std::abs((b-a)-(d-c))>bar_width*.15) continue;
            const double width=bar_width*1.2,height=bar_height*1.6;
            const double x=frame.width/2.-width/2;
            // Either horizontal HUD border may be visible through the scene.
            for (const double top:{y+bar_height*1.08,y+bar_height*.08}) {
                if (top+height>frame.height) continue;
                const Rect region{x/frame.width,top/frame.height,width/frame.width,height/frame.height};
                // Cooldown captions keep the banner below the HUD, but hide the E key.
                if (interaction_cooldown(frame,region)) return PromptRegion{region,false};
                const auto label=interaction_label(interaction_text_image(frame,region));
                if (label && label->text=="Collect Pollen") return PromptRegion{region,false};
            }
        }
    }
    return {};
}
bool cooldown_caption(const Frame& frame,Rect region) {
    if (!interaction_cooldown(frame,region)) return false;
    const double height=region.height*frame.height,width=region.width*frame.width;
    const int left=std::floor(region.x*frame.width),top=std::floor(region.y*frame.height);
    const int right=std::min(frame.width,int(std::ceil((region.x+region.width)*frame.width)));
    const int bottom=std::min(frame.height,int(std::ceil((region.y+region.height)*frame.height)));
    cv::Mat ink(bottom-top,right-left,CV_8UC1,cv::Scalar(0));
    for (int y=0;y<ink.rows;++y) for (int x=0;x<ink.cols;++x) {
        const auto i=(std::size_t(top+y)*frame.width+left+x)*4;
        const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
        if (std::min({r,g,b})>180 && std::max({r,g,b})-std::min({r,g,b})<50) ink.at<unsigned char>(y,x)=255;
    }
    std::vector<std::vector<cv::Point>> letters;
    cv::findContours(ink,letters,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    std::vector<cv::Rect> glyphs; int baseline=0;
    for (const auto& glyph:letters) {
        const auto r=cv::boundingRect(glyph);
        if (r.height<height*.12 || r.height>height*.65 || r.width>height*.7) continue;
        glyphs.push_back(r); baseline=std::max(baseline,r.y+r.height);
    }
    cv::Rect timer; unsigned timer_glyphs=0;
    for (const auto& r:glyphs) if (r.y+r.height>=baseline-height*.18) { timer|=r; ++timer_glyphs; }
    if (glyphs.size()<8 || timer_glyphs<3) return false;
    // A short centered second line is the wrapped timer. Small punctuation can
    // disappear when the game is scaled down, so use its layout in this case.
    if (glyphs.size()-timer_glyphs>=4 && timer.width<width*.75
        && std::abs(timer.x+timer.width/2.-ink.cols/2.)<=width*.15) return true;
    // Inline timers instead end with a colon followed by two seconds digits. No
    // station name or timer value needs to be read.
    const int padding=std::max(1,int(std::ceil(timer.height*.3)));
    const auto area=(timer+cv::Size(padding*2,padding*2)-cv::Point(padding,padding)) & cv::Rect(0,0,ink.cols,ink.rows);
    cv::Mat line(area.height,area.width,CV_8UC1,cv::Scalar(0));
    // Retain faint antialiased punctuation after locating the line with its
    // bright letters; a low threshold alone merges small caption letters.
    for (int y=0;y<line.rows;++y) for (int x=0;x<line.cols;++x) {
        const auto i=(std::size_t(top+area.y+y)*frame.width+left+area.x+x)*4;
        const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
        if (std::min({r,g,b})>100 && std::max({r,g,b})-std::min({r,g,b})<120) line.at<unsigned char>(y,x)=255;
    }
    std::vector<std::vector<cv::Point>> marks;
    cv::findContours(line,marks,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    std::vector<cv::Rect> parts;
    for (const auto& mark:marks) parts.push_back(cv::boundingRect(mark));
    // Bright dot cores remain separate when antialiased edges touch a digit.
    cv::findContours(ink(area),marks,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    for (const auto& mark:marks) {
        const auto r=cv::boundingRect(mark);
        if (r.height<=timer.height*.3 && r.width<=timer.height*.3) parts.push_back(r);
    }
    for (const auto& a:parts) for (const auto& b:parts) {
        if (a.x<line.cols*.5 || a.height>timer.height*.3 || b.height>timer.height*.3
            || a.width>timer.height*.3 || b.width>timer.height*.3
            || std::abs(a.x+a.width/2.-b.x-b.width/2.)>std::max(1.,timer.height*.1)
            || b.y-a.y<timer.height*.2 || b.y-a.y>std::ceil(timer.height*.65)) continue;
        unsigned after=0;
        for (const auto& part:parts) if (part.height>=timer.height*.5 && part.x>=a.x+a.width) ++after;
        const int tail=line.cols-a.x-padding;
        if (after>=2 && tail>=timer.height && tail<=timer.height*4) return true;
    }
    return false;
}
}
std::optional<Rect> interaction_prompt_region(const Frame& frame) {
    const auto prompt=locate_prompt(frame,false);
    return prompt?std::optional(prompt->bounds):std::nullopt;
}
std::optional<Rect> interaction_text_region(const Frame& frame) {
    const auto prompt=locate_prompt(frame,true);
    return prompt?std::optional(prompt->bounds):std::nullopt;
}
Reading<InteractionBanner> read_interaction_banner(const Frame& frame,std::uint64_t generation,const HudFeatures* hud) {
    if (!frame.valid() || !generation) return {};
    const auto inspected=hud?HudFeatures{}:inspect_hud(frame,generation);
    if (!hud) hud=&inspected;
    Reading<InteractionBanner> result{Knowledge::present,InteractionBanner::none,frame.captured,frame.window,generation};
    const bool hud_visible=game_hud_visible(*hud);
    // E and the HUD locate the panel even when its color merges into the scenery.
    if (const auto prompt=locate_prompt(frame,true)) {
        if (prompt->key && !interaction_cooldown(frame,prompt->bounds)) {
            result.value=InteractionBanner::ready;
            return result;
        }
        if (cooldown_caption(frame,prompt->bounds)) {
            result.value=InteractionBanner::cooldown;
            return result;
        }
    }
    // Locate the colored panel independently of the white HUD borders, which can
    // merge into a bright sky. Cooldowns have no adjoining E key.
    cv::Mat colored(frame.height/4,frame.width,CV_8UC1,cv::Scalar(0));
    for (int y=0;y<colored.rows;++y) for (int x=frame.width/4;x<frame.width*3/4;++x) {
        const auto i=(std::size_t(y)*frame.width+x)*4;
        const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
        if (r>=100 && r>g*1.3 && r>b*1.15) colored.at<unsigned char>(y,x)=255;
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(colored,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    std::vector<InteractionBanner> found;
    for (const auto& contour:contours) {
        const auto box=cv::boundingRect(contour);
        if (box.height<frame.height*.018 || box.height>frame.height*.1 || box.width<box.height*3 || box.width>box.height*14
            || std::abs(box.x+box.width/2.-frame.width/2.)>box.height || box.y<frame.height*.025
            || cv::contourArea(contour)<box.area()*.8) continue;
        // A solid panel or world geometry is not a text-bearing action banner.
        const Rect region{double(box.x)/frame.width,double(box.y)/frame.height,double(box.width)/frame.width,double(box.height)/frame.height};
        if (hud_visible && cooldown_caption(frame,region))
            found.push_back(InteractionBanner::cooldown);

    }
    if (found.size()==1) result.value=found.front();
    return result;
}
bool interaction_cooldown(const Frame& frame,Rect region) {
    if (!frame.valid() || !region.valid() || region.x<0 || region.y<0 || region.x+region.width>1 || region.y+region.height>1) return false;
    unsigned red=0;
    for (double dy:{.12,.88}) for (double dx:{.08,.92}) {
        const auto i=(std::size_t((region.y+region.height*dy)*frame.height)*frame.width
            +std::size_t((region.x+region.width*dx)*frame.width))*4;
        const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
        red+=r>=100 && r>g*1.3 && r>b*1.15;
    }
    return red>=3;
}
Frame interaction_text_image(const Frame& frame,Rect region) {
    if (!frame.valid() || !region.valid() || region.x<0 || region.y<0 || region.x+region.width>1 || region.y+region.height>1)
        throw std::invalid_argument("Invalid interaction text region");
    const int left=int(std::floor(region.x*frame.width)),top=int(std::floor(region.y*frame.height));
    const int right=std::min(frame.width,int(std::ceil((region.x+region.width)*frame.width)));
    const int bottom=std::min(frame.height,int(std::ceil((region.y+region.height)*frame.height)));
    Frame crop{frame.window,frame.captured,right-left+2*interaction_text_padding,bottom-top+2*interaction_text_padding,{}};
    crop.rgba.assign(std::size_t(crop.width)*crop.height*4,255);
    for (int y=0;y<bottom-top;++y) for (int x=0;x<right-left;++x) {
        const auto from=(std::size_t(y+top)*frame.width+x+left)*4;
        const auto to=(std::size_t(y+interaction_text_padding)*crop.width+x+interaction_text_padding)*4;
        const int r=frame.rgba[from],g=frame.rgba[from+1],b=frame.rgba[from+2];
        const auto value=std::min({r,g,b})>180 && std::max({r,g,b})-std::min({r,g,b})<50?0:255;
        crop.rgba[to]=crop.rgba[to+1]=crop.rgba[to+2]=value;crop.rgba[to+3]=255;
    }
    return crop;
}
std::optional<TextLine> interaction_label(const Frame& image,const std::string& target) {
    if (!image.valid() || image.width>2048 || image.height>512) return {};
    cv::Mat rgba(image.height,image.width,CV_8UC4,const_cast<std::uint8_t*>(image.rgba.data())),ink;
    cv::inRange(rgba,cv::Scalar(0,0,0,255),cv::Scalar(127,127,127,255),ink);
    cv::Rect first;
    for (int row=0;row<ink.rows;) {
        if (!cv::countNonZero(ink.row(row))) { ++row; continue; }
        const int top=row++;
        while (row<ink.rows && cv::countNonZero(ink.row(row))) ++row;
        if (row-top<6) continue; // Exclude a HUD border clipped into the caption crop.
        first=cv::boundingRect(ink.rowRange(top,row))+cv::Point(0,top);
        break;
    }
    if (first.empty()) return {};
    const auto first_ink=ink(first).clone();
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(ink,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    int height=0;
    for (const auto& contour:contours) height=std::max(height,cv::boundingRect(contour).height);
    for (const auto& contour:contours) {
        const auto box=cv::boundingRect(contour);
        const bool edge=box.x<=interaction_text_padding || box.y<=interaction_text_padding
            || box.x+box.width>=image.width-interaction_text_padding || box.y+box.height>=image.height-interaction_text_padding;
        if (box.height<height*.45 || (edge && box.width<box.height*.25))
            cv::drawContours(ink,std::vector{contour},0,cv::Scalar(0),cv::FILLED);
    }
    const auto bounds=cv::boundingRect(ink);
    if (bounds.height<8) return {};
    enum class Match { whole,first_line,phrase,first_word,words };
    struct Label { const char* text; cv::Mat pattern; Match match; float minimum{.9f}; unsigned prefix_words{}; };
    static const std::array labels{
        Label{"Make Honey",reference(detail::make_honey_width,detail::make_honey_height,detail::make_honey),Match::whole},
        Label{"Claim Hive",reference(detail::claim_hive_width,detail::claim_hive_height,detail::claim_hive),Match::whole},
        Label{"Trade Request",reference(detail::trade_request_width,detail::trade_request_height,detail::trade_request),Match::first_line},
        Label{"Stop Making Honey",reference(detail::stop_honey_width,detail::stop_honey_height,detail::stop_honey),Match::whole},
        Label{"Collect Pollen",reference(detail::flower_fields_width,detail::flower_fields_height,detail::flower_fields),Match::phrase,.87f},
        Label{"Blue Field Booster",reference(detail::blue_booster_width,detail::blue_booster_height,detail::blue_booster),Match::words,.83f,2},
        Label{"Blue Field Booster",reference(detail::blue_booster_clear_width,detail::blue_booster_clear_height,detail::blue_booster_clear),Match::words,.83f,2},
        Label{"Red Field Booster",reference(detail::red_booster_width,detail::red_booster_height,detail::red_booster),Match::words,.83f,2},
        Label{"Red Field Booster",reference(detail::red_booster_clear_width,detail::red_booster_clear_height,detail::red_booster_clear),Match::words,.83f,2},
        Label{"Field Booster",reference(detail::field_booster_width,detail::field_booster_height,detail::field_booster),Match::words,.81f,2},
        Label{"Field Booster",reference(detail::field_booster_clear_width,detail::field_booster_clear_height,detail::field_booster_clear),Match::words,.81f,2},
        Label{"Harvest",reference(detail::harvest_width,detail::harvest_height,detail::harvest),Match::first_word,.87f}};
    std::optional<TextLine> result;
    for (const auto& label:labels) {
        // A requested prefix identifies an action without replacing its full caption for other readers.
        if (target.empty()?label.match==Match::first_word:target!=label.text) continue;
        if (result && result->text==label.text) continue;
        auto area=label.match==Match::first_line || label.match==Match::first_word || label.match==Match::words?first:bounds;
        if (label.match==Match::first_word) {
            const auto words=word_boxes(first_ink);
            if (words.empty()) continue;
            area=words.front()+first.tl();
        }
        const auto score=label.match==Match::words?words_similarity(first_ink,label.pattern,label.prefix_words)
            :label.match==Match::phrase?phrase_similarity(ink(area),label.pattern)
            :similarity(label.match==Match::first_line?first_ink:ink(area),label.pattern);
        if (score<label.minimum) continue;
        if (result) return {};
        result=TextLine{label.text,{double(area.x),double(area.y),double(area.width),double(area.height)},std::clamp(score,0.f,1.f)};
    }
    return result;
}
}
