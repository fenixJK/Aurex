#include "connection.hpp"
#include "visual_labels.hpp"
#include "perception.hpp"
#include "respawn.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <set>
#include <opencv2/imgproc.hpp>

namespace hive::game {
std::vector<Rect> connection_dialog_regions(const Frame& frame) {
    if (!frame.valid()) return {};
    const int step=std::max(1,frame.width/240),w=frame.width/step,h=frame.height/step;
    cv::Mat neutral(h,w,CV_8UC1,cv::Scalar(0)),dark(h,w,CV_8UC1,cv::Scalar(0));
    for (int y=h/6;y<h*9/10;++y) for (int x=w/10;x<w*9/10;++x) {
        const auto i=(std::size_t(y*step)*frame.width+x*step)*4;
        const auto lo=std::min({frame.rgba[i],frame.rgba[i+1],frame.rgba[i+2]});
        const auto hi=std::max({frame.rgba[i],frame.rgba[i+1],frame.rgba[i+2]});
        const bool gray=lo>=15 && hi-lo<=20;
        neutral.at<unsigned char>(y,x)=gray?255:0;
        // Isolate the dark panel before considering other neutral surfaces.
        // The broader mask can merge it with the blurred world behind it.
        dark.at<unsigned char>(y,x)=gray && hi<=70?255:0;
    }
    // Remove narrow contacts with scenery at different capture scales.
    cv::morphologyEx(dark,dark,cv::MORPH_OPEN,cv::Mat::ones(3,3,CV_8UC1));
    for (const auto& mask:{dark,neutral}) {
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
        for (const auto& contour:contours) {
            const auto box=cv::boundingRect(contour);
            if (box.width>=w*.2 && box.width<w*.79 && box.height>=h*.12 && box.height<h*.7
                && box.width>box.height && std::abs(box.x+box.width*.5-w*.5)<w*.12
                && box.y<h*.6 && box.y+box.height>h*.4 && cv::contourArea(contour)>=box.area()*.8)
                return {{double(box.x*step)/frame.width,double(box.y*step)/frame.height,double(box.width*step)/frame.width,double(box.height*step)/frame.height}};
        }
    }
    return {};
}

ConnectionView read_connection_view(const Frame& frame,std::uint64_t generation,bool game_visible) {
    ConnectionView result;
    if (!frame.valid() || !generation) return result;
    const auto regions=connection_dialog_regions(frame);
    if (game_visible && regions.empty()) {
        result.connection={Knowledge::present,Connection::connected,frame.captured,frame.window,generation};
        result.reconnect={Knowledge::absent,{},frame.captured,frame.window,generation};
        result.home={Knowledge::present,false,frame.captured,frame.window,generation};
        return result;
    }
    static constexpr std::array<std::string_view,3> dialog{"Disconnected","Connection Error","Reconnect"};
    for (const auto& region:regions) {
        const auto labels=fixed_ui_labels(frame,region,dialog,LabelInk::either);
        const bool heading=std::any_of(labels.begin(),labels.end(),[](const auto& label) { return label.text!="Reconnect"; });
        if (!heading) continue;
        result.connection={Knowledge::present,Connection::disconnected,frame.captured,frame.window,generation};
        for (const auto& label:labels) if (label.text=="Reconnect") {
            if (result.reconnect.knowledge==Knowledge::present) {result.reconnect={};break;}
            const auto& r=label.pixels;
            result.reconnect={Knowledge::present,{(r.x+r.width/2)/frame.width,(r.y+r.height/2)/frame.height},frame.captured,frame.window,generation};
        }
        return result;
    }
    if (!game_visible && resource_hud_behind_menu(frame)) {
        const auto menu=read_respawn_menu(frame,generation);
        if (menu.knowledge==Knowledge::present && menu.value.screen==RespawnScreen::menu) {
            result.connection={Knowledge::present,Connection::connected,frame.captured,frame.window,generation};
            result.home={Knowledge::present,false,frame.captured,frame.window,generation};
            return result;
        }
    }
    if (!game_visible) {
        static constexpr std::array<std::string_view,4> navigation{"Home","Charts","Avatar","Search"};
        const auto labels=fixed_ui_labels(frame,{0,0,1,1},navigation,LabelInk::either);
        std::set<std::string> unique;for (const auto& label:labels) unique.insert(label.text);
        result.home={Knowledge::present,unique.contains("Home") && unique.size()>=3,frame.captured,frame.window,generation};
    }
    return result;
}
}
