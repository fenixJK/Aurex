#include "connection.hpp"
#include "dialog.hpp"
#include <cctype>
#include <algorithm>
#include <cmath>
#include <set>
#include <opencv2/imgproc.hpp>

namespace hive::game {
std::vector<Rect> connection_text_regions(const Frame& frame,bool in_game) {
    if (!frame.valid()) return {};
    if (!in_game) return {{0,0,1,1}};
    const int step=std::max(1,frame.width/240),w=frame.width/step,h=frame.height/step;
    cv::Mat neutral(h,w,CV_8UC1,cv::Scalar(0));
    for (int y=h/6;y<h*9/10;++y) for (int x=w/10;x<w*9/10;++x) {
        const auto i=(std::size_t(y*step)*frame.width+x*step)*4;
        const auto lo=std::min({frame.rgba[i],frame.rgba[i+1],frame.rgba[i+2]});
        const auto hi=std::max({frame.rgba[i],frame.rgba[i+1],frame.rgba[i+2]});
        neutral.at<unsigned char>(y,x)=lo>=15 && hi-lo<=20?255:0;
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(neutral,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    for (const auto& contour:contours) {
        const auto box=cv::boundingRect(contour);
        if (box.width>=w*.2 && box.width<w*.79 && box.height>=h*.12 && box.height<h*.7
            && box.width>box.height && std::abs(box.x+box.width*.5-w*.5)<w*.12
            && box.y<h*.6 && box.y+box.height>h*.4 && cv::contourArea(contour)>=box.area()*.8)
            return {{.1,.15,.8,.75}};
    }
    return {};
}

Reading<Connection> read_connection(const Frame& frame,const std::vector<TextLine>& lines,std::uint64_t generation) {
    Reading<Connection> result{Knowledge::unknown,{},frame.captured,frame.window,generation};
    if(!frame.valid()) return result;
    bool honey=false,pollen=false,disconnected=false;
    for(const auto& line:lines) {
        const auto& r=line.pixels;
        if(!r.valid() || r.x<0 || r.y<0 || r.x+r.width>frame.width || r.y+r.height>frame.height
            || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.5f || *line.confidence>1))) continue;
        std::string name;
        for(unsigned char c:line.text) {
            if(std::isspace(c)) { if(!name.empty() && name.back()!=' ') name+=' '; }
            else name+=static_cast<char>(std::tolower(c));
        }
        if(!name.empty() && name.back()==' ') name.pop_back();
        const auto x=(r.x+r.width/2)/frame.width,y=(r.y+r.height/2)/frame.height;
        if(x>=.3 && x<=.7 && y>=.2 && y<=.8 && (name=="disconnected" || name=="connection error")) disconnected=true;
        if(y<.2) {
            honey=honey || name=="honey" || name.starts_with("honey ");
            pollen=pollen || name=="pollen" || name.starts_with("pollen ");
        }
    }
    if(disconnected) result={Knowledge::present,Connection::disconnected,frame.captured,frame.window,generation};
    else if(honey && pollen) result={Knowledge::present,Connection::connected,frame.captured,frame.window,generation};
    return result;
}
bool client_home_screen(const TextSnapshot& text) {
    if (text.width<300 || text.height<200) return false;
    std::set<std::string> labels;
    for (const auto& line:text.lines) {
        const auto& r=line.pixels;
        if (!r.valid() || r.x<0 || r.y<0 || r.x+r.width>text.width || r.y+r.height>text.height
            || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.65f))) continue;
        std::string name;
        for (unsigned char c:line.text) if (!std::isspace(c)) name+=static_cast<char>(std::tolower(c));
        if (name=="home" || name=="charts" || name=="avatar" || name=="search") labels.insert(std::move(name));
    }
    return labels.contains("home") && labels.size()>=3;
}
Observation<Point> read_reconnect(const Context& context,const Reading<TextSnapshot>& text) {
    const PromptLandmark heading{{"Disconnected","Connection Error"},{.2,.2,.6,.65},.8f};
    const auto right=read_dialog(context,text,heading,{"Leave","Reconnect"});
    if (right.has_value(context,Time(2000)))
        return {Knowledge::present,right.value.no,right.captured,right.window,right.generation};
    const auto left=read_dialog(context,text,heading,{"Reconnect","Leave"});
    if (left.has_value(context,Time(2000)))
        return {Knowledge::present,left.value.yes,left.captured,left.window,left.generation};
    return {};
}
}
