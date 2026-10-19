#pragma once
#include "core/runtime/model.hpp"
#include <algorithm>
#include <vector>
#include <opencv2/imgproc.hpp>

namespace hive::game::detail {
// Locate the colored geometry before reading the label associated with it.
template<class Color> std::vector<Rect> bar_regions(const Frame& frame,Color color) {
    if (!frame.valid()) return {};
    cv::Mat mask(frame.height,frame.width,CV_8UC1,cv::Scalar(0));
    const auto end=std::size_t(frame.width)*frame.height;
    const auto* pixels=frame.rgba.data();
    auto* output=mask.data;
    // Contiguous RGBA reads let the compiler vectorize the color classification.
    for (auto i=std::size_t(frame.height/10)*frame.width;i<end;++i)
        output[i]=color(pixels+i*4)?255:0;
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    std::vector<Rect> result;
    for (const auto& contour:contours) {
        const auto b=cv::boundingRect(contour);
        if (b.width<frame.width*.015 || b.width>frame.width*.6 || b.height<2 || b.height>frame.height*.08
            || b.width<b.height*4 || b.width>b.height*65 || cv::contourArea(contour)<b.area()*.65) continue;
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
