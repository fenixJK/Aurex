#include "hive/game/detection/dialog.hpp"
#include <algorithm>
#include <cctype>
#include <opencv2/imgproc.hpp>

namespace hive::game {
namespace {
std::string label(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isspace(c)) { if (!out.empty() && out.back()!=' ') out+=' '; }
        else out+=static_cast<char>(std::tolower(c));
    }
    if (!out.empty() && out.back()==' ') out.pop_back();
    return out;
}
}
Observation<DialogButtons> read_confirmation(const Frame& frame,std::uint64_t generation) {
    Observation<DialogButtons> result{Knowledge::unknown,{},frame.captured,frame.window,generation};
    if (!frame.valid() || !generation) return result;
    const int step=std::max(1,frame.width/700),w=frame.width/step,h=frame.height/step;
    cv::Mat blue(h,w,CV_8UC1,cv::Scalar(0)),green=blue.clone(),red=blue.clone();
    for (int y=h*15/100;y<h*85/100;++y) for (int x=w*15/100;x<w*85/100;++x) {
        const auto i=(std::size_t(y*step)*frame.width+x*step)*4;
        const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
        blue.at<unsigned char>(y,x)=b>=150 && b>r*1.3 && b>g*1.15?255:0;
        green.at<unsigned char>(y,x)=g>=70 && g>r*1.2 && g>b*1.1?255:0;
        red.at<unsigned char>(y,x)=r>=120 && r>g*1.3 && r>b*1.3?255:0;
    }
    auto boxes=[](const cv::Mat& mask) {
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
        std::vector<cv::Rect> found;
        for (const auto& contour:contours) {
            const auto box=cv::boundingRect(contour);
            if (box.area()>20 && cv::contourArea(contour)>=box.area()*.8) found.push_back(box);
        }
        return found;
    };
    result.knowledge=Knowledge::absent;
    bool found=false;
    for (const auto& panel:boxes(blue)) {
        if (panel.width<w*.18 || panel.width>w*.7 || panel.height<h*.1 || panel.height>h*.5
            || panel.width<panel.height*1.5 || panel.width>panel.height*5
            || std::abs(panel.x+panel.width/2.-w/2.)>w*.12) continue;
        if (!found) result.knowledge=Knowledge::unknown;
        auto buttons=[&](const cv::Mat& mask) {
            auto candidates=boxes(mask(panel));
            std::erase_if(candidates,[&](const auto& box) {
                return box.width<panel.width*.25 || box.width>panel.width*.45
                    || box.height<panel.height*.15 || box.height>panel.height*.35
                    || box.y<panel.height*.55 || box.y+box.height>panel.height*.99;
            });
            return candidates;
        };
        const auto yes=buttons(green),no=buttons(red);
        if (yes.size()!=1 || no.size()!=1) continue;
        const auto& a=yes.front(); const auto& b=no.front();
        if (a.x+a.width>=panel.width*.5 || b.x<=panel.width*.5
            || std::abs(a.y-b.y)>panel.height*.04 || std::abs(a.height-b.height)>panel.height*.04
            || std::abs(a.width-b.width)>panel.width*.05) continue;
        if (found) { result.knowledge=Knowledge::unknown; return result; }
        auto center=[&](const auto& box) { return Point{(panel.x+box.x+box.width/2.)*step/frame.width,(panel.y+box.y+box.height/2.)*step/frame.height}; };
        result.knowledge=Knowledge::present; result.value={center(a),center(b)}; found=true;
    }
    return result;
}
Observation<DialogButtons> read_dialog(const Context& context,const Reading<TextSnapshot>& text,const PromptLandmark& question,const DialogLabels& labels) {
    validate(question);
    const auto positive=label(labels.positive),negative=label(labels.negative);
    if (positive.empty() || negative.empty() || positive==negative || positive.size()>32 || negative.size()>32)
        throw std::invalid_argument("Invalid dialog button labels");
    Observation<DialogButtons> result{Knowledge::unknown,{},text.captured,text.window,text.generation};
    if (!text.has_value(context,Time(2000)) || text.value.width<=0 || text.value.height<=0
        || text.value.lines.empty() || text.value.lines.size()>4096) return result;
    const double w=text.value.width,h=text.value.height;
    const auto& r=question.region;
    const Rect bounds{r.x*w,r.y*h,r.width*w,r.height*h};
    const TextLine *yes=nullptr,*no=nullptr;
    bool uncertain=false;
    for (const auto& line : text.value.lines) {
        if (line.text.size()>512) return result;
        const auto word=label(line.text);
        if (word!=positive && word!=negative) continue;
        const auto& b=line.pixels;
        if (!b.valid()) { uncertain=true; continue; }
        if (b.x>=bounds.x+bounds.width || b.y>=bounds.y+bounds.height || b.x+b.width<=bounds.x || b.y+b.height<=bounds.y) continue;
        if (b.x<bounds.x || b.y<bounds.y || b.x+b.width>bounds.x+bounds.width || b.y+b.height>bounds.y+bounds.height
            || line.text.size()>512 || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<std::min(question.confidence,.5f) || *line.confidence>1))) {
            uncertain=true; continue;
        }
        if (word==positive) { if (yes) return result; yes=&line; }
        if (word==negative) { if (no) return result; no=&line; }
    }
    if (uncertain) return result;
    if (!yes && !no) {
        if (read_prompt_landmark(context,text,question).has_value(context,Time(2000))) return result;
        for (const auto& line : text.value.lines) {
            const auto& b=line.pixels;
            if (!b.valid() || b.x>=bounds.x+bounds.width || b.y>=bounds.y+bounds.height || b.x+b.width<=bounds.x || b.y+b.height<=bounds.y) continue;
            const auto fragment=label(line.text);
            if (fragment.empty()) continue;
            for (const auto& phrase : question.phrases) {
                const auto expected=label(phrase);
                if (fragment==expected || (fragment.size()>=4 && expected.find(fragment)!=std::string::npos)
                    || (question.match==PromptMatch::phrase && fragment.find(expected)!=std::string::npos)) return result;
            }
        }
        result.knowledge=Knowledge::absent;
        return result;
    }
    if (!yes || !no) return result;
    const auto& a=yes->pixels; const auto& b=no->pixels;
    if (a.x+a.width>=b.x || std::abs((a.y+a.height/2)-(b.y+b.height/2))>std::max(a.height,b.height)*.5
        || a.height<b.height*.6 || b.height<a.height*.6 || b.x-a.x>bounds.width*.9) return result;
    auto above=question;
    above.region.height=(std::min(a.y,b.y)-bounds.y)/h;
    if (above.region.height<=0 || !read_prompt_landmark(context,text,above).has_value(context,Time(2000))) return result;
    result.knowledge=Knowledge::present;
    result.value={{(a.x+a.width/2)/w,(a.y+a.height/2)/h},{(b.x+b.width/2)/w,(b.y+b.height/2)/h}};
    return result;
}
}
