#include "landmarks.hpp"
#include "perception.hpp"
#include "text.hpp"
#include <algorithm>
#include <cctype>
#include <opencv2/imgproc.hpp>

namespace hive::game {
namespace {
std::string normalize(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isspace(c)) { if (!out.empty() && out.back()!=' ') out+=' '; }
        else out+=static_cast<char>(std::tolower(c));
    }
    if (!out.empty() && out.back()==' ') out.pop_back();
    return out;
}
std::string words(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c)) out+=static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back()!=' ') out+=' ';
    }
    if (!out.empty() && out.back()==' ') out.pop_back();
    return out;
}
bool matches(const std::string& text,const std::string& phrase,PromptMatch mode) {
    if (mode==PromptMatch::exact) return words(text)==words(phrase);
    if (mode==PromptMatch::prefix) return (words(text)+" ").starts_with(words(phrase)+" ");
    return (" "+words(text)+" ").find(" "+words(phrase)+" ")!=std::string::npos;
}
}
std::optional<Rect> interaction_prompt_region(const Frame& frame) {
    if (!frame.valid()) return {};
    const int top=frame.height/25,bottom=frame.height/4;
    cv::Mat mask(bottom-top,frame.width,CV_8UC1,cv::Scalar(0));
    for (int y=top;y<bottom;++y) for (int x=frame.width/5;x<frame.width*4/5;++x) {
        const auto i=(std::size_t(y)*frame.width+x)*4;
        const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
        if (b>=100 && b>r*1.3 && b>g*1.15) mask.at<unsigned char>(y-top,x)=255;
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    std::optional<Rect> found;
    for (const auto& contour:contours) {
        const auto box=cv::boundingRect(contour);
        const int x=box.x,y=box.y+top,w=box.width,h=box.height;
        if (h<frame.height*.018 || h>frame.height*.13 || w<h*1.5 || w>h*18
            || x>frame.width*.55 || x+w<frame.width*.5 || x<h
            || cv::contourArea(contour)<w*double(h)*.8) continue;
        unsigned neutral=0;
        for (double dy:{.15,.5,.85}) for (double dx:{.15,.85}) {
            const auto i=(std::size_t(y+int(h*dy))*frame.width+x-int(h*dx))*4;
            const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
            neutral+=r>=170 && std::max({r,g,b})-std::min({r,g,b})<25;
        }
        if (neutral<5) continue;
        if (found) return {};
        found=Rect{double(x)/frame.width,double(y)/frame.height,double(w)/frame.width,double(h)/frame.height};
    }
    if (found) return found;
    cv::Mat keys(bottom-top,frame.width,CV_8UC1,cv::Scalar(0));
    for (int y=top;y<bottom;++y) for (int x=frame.width/4;x<frame.width*3/5;++x) {
        const auto i=(std::size_t(y)*frame.width+x)*4;
        const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
        if (std::min({r,g,b})>=170 && std::max({r,g,b})-std::min({r,g,b})<25) keys.at<unsigned char>(y-top,x)=255;
    }
    contours.clear(); cv::findContours(keys,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    for (const auto& contour:contours) {
        const auto key=cv::boundingRect(contour);
        const int y=key.y+top,h=key.height,x=key.x+key.width;
        if (h<frame.height*.018 || h>frame.height*.13 || key.width<h*.8 || key.width>h*1.2
            || cv::contourArea(contour)<key.area()*.75 || x+h*2>=frame.width) continue;
        const int inset=std::max(2,h/12);
        const auto base=(std::size_t(y+inset)*frame.width+x+inset)*4;
        const int r=frame.rgba[base],g=frame.rgba[base+1],b=frame.rgba[base+2];
        if (b<100 || b<=r*1.3 || b<=g*1.15) continue;
        const int edge=frame.width-key.x;
        if (edge-x<h*1.5 || edge-x>=h*18 || edge>frame.width) continue;
        if (found) return {};
        found=Rect{double(x)/frame.width,double(y)/frame.height,double(edge-x)/frame.width,double(h)/frame.height};
    }
    return found;
}
std::optional<Rect> interaction_text_region(const Frame& frame,const HudFeatures* hud) {
    if (!frame.valid()) return {};
    if (const auto prompt=interaction_prompt_region(frame)) return prompt;
    const auto resources=hud?hud->resources:resource_hud_regions(frame);
    if (!resources) return {};
    const auto& bar=(*resources)[1];
    const Rect banner{bar.x-bar.width*.6,bar.y+bar.height,bar.width*1.2,bar.height*1.6};
    if (banner.x<0 || banner.y<0 || banner.x+banner.width>frame.width || banner.y+banner.height>frame.height) return {};
    unsigned blue=0;
    for (double dy:{.15,.5,.85}) for (double dx:{.05,.25,.5,.75,.95}) {
        const auto i=(std::size_t(banner.y+banner.height*dy)*frame.width+int(banner.x+banner.width*dx))*4;
        const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
        blue+=b>=100 && b>r*1.3 && b>g*1.15;
    }
    if (blue<10) return {};
    return Rect{banner.x/frame.width,banner.y/frame.height,banner.width/frame.width,banner.height/frame.height};
}
Frame interaction_text_image(const Frame& frame,Rect region) {
    if (!frame.valid() || !region.valid() || region.x<0 || region.y<0 || region.x+region.width>1 || region.y+region.height>1)
        throw std::invalid_argument("Invalid interaction text region");
    const int left=int(std::floor(region.x*frame.width)),top=int(std::floor(region.y*frame.height));
    const int right=std::min(frame.width,int(std::ceil((region.x+region.width)*frame.width)));
    const int bottom=std::min(frame.height,int(std::ceil((region.y+region.height)*frame.height)));
    Frame crop{frame.window,frame.captured,right-left,bottom-top,{}};
    crop.rgba.resize(std::size_t(crop.width)*crop.height*4);
    for (int y=0;y<crop.height;++y) for (int x=0;x<crop.width;++x) {
        const auto from=(std::size_t(y+top)*frame.width+x+left)*4,to=(std::size_t(y)*crop.width+x)*4;
        const int r=frame.rgba[from],g=frame.rgba[from+1],b=frame.rgba[from+2];
        const auto value=std::min({r,g,b})>180 && std::max({r,g,b})-std::min({r,g,b})<50?0:255;
        crop.rgba[to]=crop.rgba[to+1]=crop.rgba[to+2]=value; crop.rgba[to+3]=255;
    }
    return crop;
}
void validate(const PromptLandmark& p) {
    const auto& r=p.region;
    if ((p.match!=PromptMatch::exact && p.match!=PromptMatch::phrase && p.match!=PromptMatch::prefix) || p.phrases.empty() || p.phrases.size()>16 || !r.valid() || r.x<0 || r.y<0
        || r.x+r.width>1 || r.y+r.height>1 || !std::isfinite(p.confidence) || p.confidence<0 || p.confidence>1)
        throw std::invalid_argument("Invalid prompt landmark");
    for (const auto& phrase : p.phrases)
        if (phrase.size()>120 || words(phrase).empty()) throw std::invalid_argument("Invalid landmark phrase");
}
Reading<bool> read_prompt_landmark(const Context& c,const Reading<TextSnapshot>& text,const PromptLandmark& p) {
    validate(p);
    Reading<bool> result{Knowledge::unknown,false,text.captured,text.window,text.generation};
    if (!text.has_value(c,Time(2000)) || text.value.width<=0 || text.value.height<=0) return result;
    const auto& snapshot=text.value;
    const Rect bounds{p.region.x*snapshot.width,p.region.y*snapshot.height,
        p.region.width*snapshot.width,p.region.height*snapshot.height};
    if (snapshot.lines.size()>4096) return result;
    bool uncertain=false;
    std::vector<TextLine> candidates;
    for (const auto& line : snapshot.lines) {
        const auto& r=line.pixels;
        if (!r.valid() || r.x<bounds.x || r.y<bounds.y || r.x+r.width>bounds.x+bounds.width
            || r.y+r.height>bounds.y+bounds.height || line.text.size()>512) continue;
        if (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<p.confidence || *line.confidence>1)) {
            if (!p.ignore_unrelated_uncertainty) { uncertain=true; continue; }
            const auto observed=" "+words(line.text)+" ";
            for (const auto& phrase:p.phrases) {
                const auto target=words(phrase);
                for (std::size_t begin=0;begin<target.size();) {
                    const auto end=target.find(' ',begin);
                    const auto word=target.substr(begin,end==std::string::npos?end:end-begin);
                    if (word.size()>=4 && observed.find(" "+word+" ")!=std::string::npos) uncertain=true;
                    if (end==std::string::npos) break;
                    begin=end+1;
                }
            }
            continue;
        }
        candidates.push_back(line);
    }
    for (const auto& line:text_candidates(candidates)) {
        auto normalized=normalize(line.text);
        if (normalized.starts_with("e ")) normalized.erase(0,2);
        if (std::any_of(p.phrases.begin(),p.phrases.end(),[&](const auto& phrase) { return matches(normalized,phrase,p.match); })) {
            result.knowledge=Knowledge::present; result.value=true; return result;
        }
    }
    if (!uncertain) result.knowledge=Knowledge::absent;
    return result;
}
PromptLandmark hive_proximity_prompt() { return {{"Make Honey","Stop Making Honey","Collect Pollen","Flower Fields"},
    {.2,0,.6,.3},hive_prompt_confidence,PromptMatch::phrase,true}; }
PromptLandmark hive_claim_prompt() { return {{"Claim Hive"},{.2,0,.6,.3},hive_prompt_confidence,PromptMatch::exact,true}; }
}
