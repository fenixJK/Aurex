#include "landmarks.hpp"
#include "prompt_labels.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <span>
#include <stdexcept>
#include <vector>

namespace hive::game {
namespace {
cv::Mat reference(int width,int height,std::span<const std::uint64_t> bits) {
    cv::Mat image(height,width,CV_8UC1);
    const int stride=(width+63)/64;
    for (int y=0;y<height;++y) for (int x=0;x<width;++x)
        image.at<unsigned char>(y,x)=bits[y*stride+x/64]&(1ULL<<(x%64))?255:0;
    return image;
}
float similarity(const cv::Mat& ink,const cv::Mat& pattern) {
    if (ink.rows<6 || ink.cols<2) return 0;
    const double aspect=double(pattern.cols)/pattern.rows;
    if (std::abs(ink.cols-aspect*ink.rows)>std::max(3.,aspect*1.5+ink.cols*.025)) return 0;
    const int height=std::min({32,ink.rows,pattern.rows});
    const cv::Size size(std::max(1,int(std::round(double(pattern.cols)*height/pattern.rows))),height);
    cv::Mat normalized,scaled,score;
    cv::resize(pattern,normalized,size,0,0,cv::INTER_AREA);
    cv::GaussianBlur(normalized,normalized,{3,3},1.0);
    double best=0;
    for (int delta:{0,-1,1,-2,2}) {
        cv::resize(ink,scaled,{size.width,size.height+delta},0,0,cv::INTER_AREA);
        cv::GaussianBlur(scaled,scaled,{3,3},1.0);
        cv::copyMakeBorder(scaled,scaled,3,3,2,2,cv::BORDER_CONSTANT,cv::Scalar(0));
        cv::matchTemplate(scaled,normalized,score,cv::TM_CCOEFF_NORMED);
        double value=0;cv::minMaxLoc(score,nullptr,&value);best=std::max(best,value);
        if (best>=.94) break;
    }
    return static_cast<float>(best);
}
std::vector<cv::Rect> word_boxes(const cv::Mat& line) {
    std::vector<cv::Rect> words;
    for (int x=0;x<line.cols;) {
        if (!cv::countNonZero(line.col(x))) { ++x; continue; }
        const int left=x++;
        int right=x;
        while (x<line.cols && x-right<line.rows*.25) {
            if (cv::countNonZero(line.col(x))) right=x+1;
            ++x;
        }
        words.push_back(cv::boundingRect(line.colRange(left,right))+cv::Point(left,0));
    }
    return words;
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
std::optional<Rect> locate_prompt(const Frame& frame,bool captions) {
    if (!frame.valid()) return {};
    cv::Mat light(frame.height/4,frame.width,CV_8UC1,cv::Scalar(0));
    for (int y=0;y<light.rows;++y) for (int x=frame.width/5;x<frame.width*4/5;++x) {
        const auto i=(std::size_t(y)*frame.width+x)*4;
        const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
        if (std::min({r,g,b})>=170 && std::max({r,g,b})-std::min({r,g,b})<65) light.at<unsigned char>(y,x)=255;
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(light,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    std::optional<Rect> found;
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
        found=Rect{double(left)/frame.width,double(y)/frame.height,double(right-left)/frame.width,double(height)/frame.height};
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
                const auto label=interaction_label(interaction_text_image(frame,region));
                if (label && label->text=="Collect Pollen") return region;
            }
        }
    }
    return {};
}
}
std::optional<Rect> interaction_prompt_region(const Frame& frame) { return locate_prompt(frame,false); }
std::optional<Rect> interaction_text_region(const Frame& frame) { return locate_prompt(frame,true); }
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
    auto first=cv::boundingRect(ink);
    if (first.empty()) return {};
    for (int row=first.y;row<first.y+first.height;++row) if (!cv::countNonZero(ink.row(row))) {first.height=row-first.y;break;}
    first=cv::boundingRect(ink(first))+first.tl();
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
    enum class Match { whole,first_line,phrase,first_word };
    struct Label { const char* text; cv::Mat pattern; Match match; float minimum{.9f}; };
    static const std::array labels{
        Label{"Make Honey",reference(detail::make_honey_width,detail::make_honey_height,detail::make_honey),Match::whole},
        Label{"Claim Hive",reference(detail::claim_hive_width,detail::claim_hive_height,detail::claim_hive),Match::whole},
        Label{"Trade Request",reference(detail::trade_request_width,detail::trade_request_height,detail::trade_request),Match::first_line},
        Label{"Stop Making Honey",reference(detail::stop_honey_width,detail::stop_honey_height,detail::stop_honey),Match::whole},
        Label{"Collect Pollen",reference(detail::flower_fields_width,detail::flower_fields_height,detail::flower_fields),Match::phrase,.87f},
        Label{"Harvest",reference(detail::harvest_width,detail::harvest_height,detail::harvest),Match::first_word,.87f}};
    std::optional<TextLine> result;
    for (const auto& label:labels) {
        // A requested prefix identifies an action without replacing its full caption for other readers.
        if (target.empty()?label.match==Match::first_word:target!=label.text) continue;
        auto area=label.match==Match::first_line || label.match==Match::first_word?first:bounds;
        if (label.match==Match::first_word) {
            const auto words=word_boxes(first_ink);
            if (words.empty()) continue;
            area=words.front()+first.tl();
        }
        const auto score=label.match==Match::phrase?phrase_similarity(ink(area),label.pattern)
            :similarity(label.match==Match::first_line?first_ink:ink(area),label.pattern);
        if (score<label.minimum) continue;
        if (result) return {};
        result=TextLine{label.text,{double(area.x),double(area.y),double(area.width),double(area.height)},std::clamp(score,0.f,1.f)};
    }
    return result;
}
}
