#include "item_menu.hpp"
#include "conversation.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cctype>
#include <regex>

namespace hive::game {
const std::vector<std::string>& blender_recipes() {
    static const std::vector<std::string> names{"Red Extract","Blue Extract","Enzymes","Oil","Glue","Tropical Drink","Gumdrops",
        "Moon Charms","Glitter","Star Jelly","Purple Potion","Soft Wax","Hard Wax","Swirled Wax","Caustic Wax",
        "Field Dice","Smooth Dice","Loaded Dice","Super Smoothie","Turpentine"};
    return names;
}
const std::vector<std::string>& shrine_items() {
    static const std::vector<std::string> names{"Cloud Vial","Red Extract","Blue Extract","Blueberry","Pineapple","Strawberry",
        "Sunflower Seed","Enzymes","Oil","Glue","Tropical Drink","Gumdrops","Moon Charm","Glitter","Star Jelly","Purple Potion",
        "Ant Pass","Soft Wax","Hard Wax","Swirled Wax","Caustic Wax","Field Dice","Smooth Dice","Loaded Dice","Turpentine",
        "Ticket","Coconut","Stinger","Micro-Converter","Honeysuckle","Whirligig","Jelly Beans","Magic Bean","Treat",
        "Bitterberry","Neonberry","Royal Jelly","Basic Egg","Silver Egg","Gold Egg","Diamond Egg","Mythic Egg",
        "Star Egg","Gifted Silver Egg","Gifted Gold Egg","Gifted Diamond Egg","Gifted Mythic Egg"};
    return names;
}
namespace {
std::string words(std::string_view text) {
    std::string out;
    for (const unsigned char c:text) if (std::isalnum(c)) out+=char(std::tolower(c));
    else if (!out.empty() && out.back()!=' ') out+=' ';
    if (!out.empty() && out.back()==' ') out.pop_back();
    return out;
}
bool contains(const cv::Rect& box,const Rect& line) {
    return line.valid() && line.x>=box.x && line.y>=box.y && line.x+line.width<=box.x+box.width && line.y+line.height<=box.y+box.height;
}
std::vector<cv::Rect> panels(const Frame& frame) {
    if (!frame.valid() || frame.width>8192 || frame.height>8192) return {};
    const int step=std::max(1,frame.width/800),w=frame.width/step,h=frame.height/step;
    cv::Mat blue(h,w,CV_8UC1,cv::Scalar(0));
    for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
        const auto i=(std::size_t(y*step)*frame.width+x*step)*4;
        const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
        blue.at<unsigned char>(y,x)=b>=80 && b>r*1.35 && b>g*1.15?255:0;
    }
    cv::morphologyEx(blue,blue,cv::MORPH_CLOSE,cv::getStructuringElement(cv::MORPH_RECT,{3,3}));
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(blue,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    std::vector<cv::Rect> result;
    for (const auto& contour:contours) {
        const auto b=cv::boundingRect(contour);
        if (b.width<w*.12 || b.height<h*.14 || b.width<b.height*.75 || b.width>b.height*2.5
            || cv::contourArea(contour)<b.area()*.75) continue;
        result.push_back({b.x*step,b.y*step,b.width*step,b.height*step});
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b) { return a.area()>b.area(); });
    if (result.size()>3) result.resize(3);
    return result;
}
Point center(const Frame& f,const Rect& r) { return {(r.x+r.width/2)/f.width,(r.y+r.height/2)/f.height}; }
std::optional<bool> green(const Frame& f,const Rect& r) {
    unsigned yes=0,no=0;
    for (int y=std::max(0,int(r.y));y<std::min(f.height,int(r.y+r.height));++y)
        for (int x=std::max(0,int(r.x));x<std::min(f.width,int(r.x+r.width));++x) {
            const auto at=(std::size_t(y)*f.width+x)*4;
            const int red=f.rgba[at],g=f.rgba[at+1],b=f.rgba[at+2];
            yes+=g>70 && g>red*1.3 && g>b*1.2;
            no+=red>70 && red>g*1.3 && red>b*1.2;
        }
    if (yes>20 && yes>no*3) return true;
    if (no>20 && no>yes*3) return false;
    return {};
}
std::optional<Time> timer(const std::string& text) {
    static const std::regex pattern(R"((\d{1,3}):(\d{2})(?::(\d{2}))?)");
    std::smatch match;
    if (!std::regex_search(text,match,pattern)) return {};
    const int first=std::stoi(match[1]),second=std::stoi(match[2]);
    if (second>=60) return {};
    const int third=match[3].matched?std::stoi(match[3]):0;
    if (third>=60) return {};
    const auto value=std::chrono::seconds(match[3].matched?first*3600+second*60+third:first*60+second);
    return value<=std::chrono::days(7)?std::optional<Time>{value}:std::nullopt;
}
void quantity_controls(const DetectionInput& input,const cv::Rect& panel,ItemMenu& result) {
    const auto& f=input.frame;
    cv::Mat white(panel.height,panel.width,CV_8UC1,cv::Scalar(0));
    for (int y=panel.height/2;y<panel.height;++y) for (int x=0;x<panel.width;++x) {
        const auto at=(std::size_t(panel.y+y)*f.width+panel.x+x)*4;
        const auto low=std::min({f.rgba[at],f.rgba[at+1],f.rgba[at+2]});
        white.at<unsigned char>(y,x)=low>=215?255:0;
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(white,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    std::optional<cv::Rect> plus,minus;
    for (const auto& contour:contours) {
        const auto b=cv::boundingRect(contour);
        if (b.width<panel.width*.022 || b.width>panel.width*.1 || b.height<panel.height*.025
            || b.width<b.height*.75 || b.width>b.height*1.3) continue;
        unsigned cross=0,corners=0;
        for (int y=0;y<b.height;++y) for (int x=0;x<b.width;++x) {
            const bool middle_x=x>=b.width*.38 && x<b.width*.62,middle_y=y>=b.height*.38 && y<b.height*.62;
            if (white.at<unsigned char>(b.y+y,b.x+x)) { if (middle_x||middle_y) ++cross; else ++corners; }
        }
        if (cross<b.area()*.28 || corners>b.area()*.12) continue;
        if (plus) return;
        plus=b;
    }
    if (!plus) return;
    for (const auto& contour:contours) {
        const auto b=cv::boundingRect(contour);
        if (b.x+b.width>=plus->x || b.width<plus->width*.6 || b.width>plus->width*1.4 || b.height>b.width*.5
            || std::abs(b.y+b.height/2.-(plus->y+plus->height/2.))>plus->height*.25
            || cv::contourArea(contour)<b.area()*.65) continue;
        if (!minus || b.x>minus->x) minus=b;
    }
    if (!minus) return;
    auto point=[&](const cv::Rect& b) { return Point{(panel.x+b.x+b.width/2.)/f.width,(panel.y+b.y+b.height/2.)/f.height}; };
    result.more=point(*plus); result.less=point(*minus);
    const auto number=[](const std::string& text)->std::optional<unsigned> {
        std::string digits;
        for (unsigned char c:text) if (c!=',' && !std::isspace(c)) digits+=char(c);
        if (digits.empty() || digits.size()>4 || !std::all_of(digits.begin(),digits.end(),[](char c) { return c>='0' && c<='9'; })) return {};
        return unsigned(std::stoi(digits));
    };
    for (const auto& line:input.text) {
        const auto& b=line.pixels;
        if (b.x<panel.x+minus->x+minus->width || b.x+b.width>panel.x+plus->x
            || std::abs(b.y+b.height/2-(panel.y+plus->y+plus->height/2.))>plus->height*.7) continue;
        const auto value=number(line.text);
        if (!value) continue;
        if (result.quantity) { result.quantity.reset(); return; }
        result.quantity=value;
    }
    if (!result.quantity && input.recognize) {
        const int x0=panel.x+minus->x+minus->width+2,x1=panel.x+plus->x-2;
        const int y0=std::max(panel.y,panel.y+plus->y-plus->height/4),y1=std::min(panel.y+panel.height,panel.y+plus->y+plus->height*5/4);
        if (x1<=x0 || y1<=y0) return;
        Frame crop{f.window,f.captured,x1-x0+16,y1-y0+16,{}};
        crop.rgba.assign(std::size_t(crop.width)*crop.height*4,255);
        for (int y=y0;y<y1;++y) for (int x=x0;x<x1;++x) {
            const auto from=(std::size_t(y)*f.width+x)*4,to=(std::size_t(y-y0+8)*crop.width+x-x0+8)*4;
            const bool ink=std::min({f.rgba[from],f.rgba[from+1],f.rgba[from+2]})>=215;
            crop.rgba[to]=crop.rgba[to+1]=crop.rgba[to+2]=ink?0:255;
        }
        cv::Mat pixels(crop.height,crop.width,CV_8UC4,crop.rgba.data()),ink;
        cv::inRange(pixels,cv::Scalar(0,0,0,255),cv::Scalar(0,0,0,255),ink);
        std::vector<std::vector<cv::Point>> glyphs;
        cv::findContours(ink,glyphs,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
        if (glyphs.size()==1) {
            const auto b=cv::boundingRect(glyphs.front());
            if (b.height>=plus->height*.65 && b.width>=b.height*.18 && b.width<=b.height*.45) {
                unsigned stem=0;
                for (int x=b.x;x<b.x+b.width;++x) {
                    unsigned rows=0;
                    for (int y=b.y+b.height/3;y<b.y+b.height;++y) rows+=ink.at<unsigned char>(y,x)>0;
                    stem=std::max(stem,rows);
                }
                if (stem>=unsigned(b.height-b.height/3)*.95) { result.quantity=1; return; }
            }
        }
        const int scale=std::max(1,48/(y1-y0));
        if (scale>1) {
            cv::Mat larger;
            cv::resize(pixels,larger,{},scale,scale,cv::INTER_NEAREST);
            crop.width=larger.cols; crop.height=larger.rows;
            crop.rgba.assign(larger.data,larger.data+larger.total()*4);
        }
        for (const auto& line:input.recognize(crop)) if (auto value=number(line.text)) {
            if (result.quantity) { result.quantity.reset(); return; }
            result.quantity=value;
        }
    }
}
class ItemMenus final : public Detector {
    std::shared_ptr<const Detector> conversation_{conversation_detector()};
public:
    std::vector<Rect> text_regions(const Frame& frame,std::uint64_t) const override {
        std::vector<Rect> result;
        for (const auto& b:panels(frame)) result.push_back({double(b.x)/frame.width,double(b.y)/frame.height,double(b.width)/frame.width,double(b.height)/frame.height});
        result.push_back({.1,.55,.8,.4});
        return result;
    }
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& f=input.frame;
        if (!f.valid() || input.text.size()>4096 || (input.cancelled && input.cancelled())) return {};
        auto result=std::make_shared<ItemMenu>();
        if (const auto talk=std::dynamic_pointer_cast<const ConversationDetection>(conversation_->detect(input))) result->continue_dialogue=talk->view.next;
        for (const auto& line:input.text) if (line.pixels.y>=f.height*.55 && line.text.size()<512) result->dialogue+=line.text+" ";
        const auto message=words(result->dialogue);
        if (message.find("come back in")!=std::string::npos) result->remaining=timer(result->dialogue);
        for (const auto phrase:{"does not have","not enough","cannot donate","canceled the donation","cancelled the donation","error player"})
            if (message.find(phrase)!=std::string::npos) result->error="The game could not complete this item transaction";
        for (const auto& panel:panels(f)) {
            std::string text;
            for (const auto& line:input.text) if (contains(panel,line.pixels) && line.text.size()<512) text+=words(line.text)+" ";
            const bool blender=text.find("blender")!=std::string::npos;
            const bool shrine=text.find("wind shrine")!=std::string::npos && text.find("donate")!=std::string::npos;
            const bool printer=text.find("sticker printer")!=std::string::npos;
            if (printer) {
                result->kind=ItemMenuKind::sticker_printer; result->stage=ItemMenuStage::selection;
                result->close=Point{(panel.x+panel.width*.046)/f.width,(panel.y+panel.height*.063)/f.height};
                for (unsigned i=0;i<5;++i) result->choices.push_back({(panel.x+panel.width*(.324+.103*i))/f.width,(panel.y+panel.height*.377)/f.height});
                for (const auto& line:input.text) if (contains(panel,line.pixels)) {
                    const auto label=words(line.text);
                    if (label=="confirm") {
                        result->submit=center(f,line.pixels);
                        unsigned yellow=0;
                        for (int y=int(line.pixels.y);y<int(line.pixels.y+line.pixels.height);++y)
                            for (int x=int(line.pixels.x);x<int(line.pixels.x+line.pixels.width);++x) {
                                const auto at=(std::size_t(y)*f.width+x)*4;
                                yellow+=f.rgba[at]>160 && f.rgba[at+1]>130 && f.rgba[at+2]<100;
                            }
                        result->enabled=yellow>line.pixels.width*line.pixels.height*.1;
                    }
                    if (auto remaining=timer(line.text)) result->remaining=remaining;
                }
                break;
            }
            if (!blender && !shrine) continue;
            result->kind=blender?ItemMenuKind::blender:ItemMenuKind::wind_shrine;
            auto point=[&](double x,double y) { return Point{(panel.x+x*panel.width)/f.width,(panel.y+y*panel.height)/f.height}; };
            result->stage=ItemMenuStage::selection;
            if (blender) { result->previous=point(.05,.86); result->next=point(.95,.86); result->close=point(.055,.065); }
            else { result->previous=point(.53,.43); result->next=point(.86,.43); result->stage=ItemMenuStage::quantity; }
            const auto& items=blender?blender_recipes():shrine_items();
            std::string item_text;
            for (const auto& line:input.text) if (contains(panel,line.pixels)) {
                const auto value=words(line.text);
                const auto center_y=(line.pixels.y+line.pixels.height/2-panel.y)/panel.height;
                if (center_y>.12 && center_y<(blender?.34:.57)) {
                    for (const auto& name:items) if (value==words(name) || value=="make "+words(name)) result->item=name;
                    if (shrine && center_y>.34 && line.pixels.x<panel.x+panel.width*.5 && value.size()>2 && value!="wind shrine") {
                        if (!item_text.empty()) item_text+=' ';
                        item_text+=line.text;
                    }
                }
                if (value=="end crafting") {
                    result->stage=ItemMenuStage::crafting; result->submit=center(f,line.pixels); result->finished=green(f,line.pixels);
                } else if (value=="confirm" || value=="donate" || value.starts_with("make ")) {
                    result->submit=center(f,line.pixels); result->enabled=green(f,line.pixels);
                    if (value=="confirm") result->stage=ItemMenuStage::quantity;
                } else if (value=="cancel") result->cancel=center(f,line.pixels);
                if (value.find("remaining")!=std::string::npos || value.find("time")!=std::string::npos)
                    if (auto remaining=timer(line.text)) result->remaining=remaining;
            }
            if (shrine && !item_text.empty()) {
                result->item=item_text;
                for (const auto& name:items) if (words(name)==words(item_text)) { result->item=name; break; }
            }
            if (result->stage==ItemMenuStage::crafting && !result->remaining) {
                for (const auto& line:input.text) if (contains(panel,line.pixels)) if (auto remaining=timer(line.text)) result->remaining=remaining;
            }
            if (result->stage==ItemMenuStage::quantity) quantity_controls(input,panel,*result);
            break;
        }
        return result;
    }
};
}
std::shared_ptr<const Detector> item_menu_detector() { return std::make_shared<ItemMenus>(); }
}
