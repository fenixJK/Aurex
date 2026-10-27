#pragma once
#include "core/runtime/model.hpp"
#include <algorithm>
#include <vector>
#include <opencv2/imgproc.hpp>

namespace hive::game::detail {
// Locate the colored geometry before reading the label associated with it.
template<class Color> std::vector<Rect> bar_regions(const Frame& frame,Color color,bool labelled=false) {
    if (!frame.valid()) return {};
    cv::Mat mask(frame.height,frame.width,CV_8UC1,cv::Scalar(0));
    const auto end=std::size_t(frame.width)*frame.height;
    const auto* pixels=frame.rgba.data();
    auto* output=mask.data;
    // Contiguous RGBA reads let the compiler vectorize the color classification.
    for (auto i=std::size_t(frame.height/10)*frame.width;i<end;++i)
        output[i]=color(pixels+i*4)?255:0;
    if (labelled) {
        // Keep horizontal label bars separate from thin foliage and particles.
        cv::morphologyEx(mask,mask,cv::MORPH_OPEN,
            cv::getStructuringElement(cv::MORPH_RECT,{std::max(3,frame.width/256),1}));
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    std::vector<cv::Rect> boxes;
    for (const auto& contour:contours) {
        const auto b=cv::boundingRect(contour);
        if (b.height<2 || b.height>frame.height*.08 || b.width<b.height || b.width>frame.width*.6) continue;
        if (!labelled && cv::contourArea(contour)<b.area()*.65) continue;
        boxes.push_back(b);
    }
    if (labelled) {
        // HP/growth text can cut a bar into separate colored pieces. Join only
        // nearby fragments with the same top and height; the caller still has
        // to recognize the label before treating a candidate as evidence.
        std::sort(boxes.begin(),boxes.end(),[](auto a,auto b) { return a.x<b.x; });
        for (std::size_t i=0;i<boxes.size();++i) for (std::size_t j=i+1;j<boxes.size();) {
            const auto a=boxes[i],b=boxes[j];
            const int height=std::max(a.height,b.height),gap=b.x-(a.x+a.width);
            if (gap>frame.height*.24) break;
            if (gap>height*3) { ++j; continue; }
            if (gap>=0 && std::abs(a.y-b.y)<=height*.25 && std::abs(a.height-b.height)<=height*.25) {
                boxes[i]|=b;boxes.erase(boxes.begin()+j);
            } else ++j;
        }
    }
    std::vector<Rect> result;
    for (const auto& b:boxes) {
        // Label validation can separate a bar from attached scenery; retain a
        // loose horizontal-shape bound here to exclude compact world objects.
        if (b.width<frame.width*.015 || b.width>frame.width*.6 || b.height<2 || b.height>frame.height*.08
            || b.width<b.height*(labelled?3:4) || b.width>b.height*65) continue;
        result.push_back({double(b.x),double(b.y),double(b.width),double(b.height)});
    }
    return result;
}
inline Rect label_region(const Frame& frame,Rect bar,double above,double below) {
    const double text_height=std::max(bar.height,bar.width/20.);
    const double left=std::max(0.,bar.x-text_height),top=std::max(0.,bar.y-text_height*above);
    const double right=std::min(double(frame.width),bar.x+bar.width+text_height);
    const double bottom=std::min(double(frame.height),bar.y+bar.height+text_height*below);
    return {left/frame.width,top/frame.height,(right-left)/frame.width,(bottom-top)/frame.height};
}
}
