#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <opencv2/imgproc.hpp>

namespace hive::game::detail {
inline cv::Mat reference(int width,int height,std::span<const std::uint64_t> bits) {
    cv::Mat image(height,width,CV_8UC1);
    const int stride=(width+63)/64;
    for (int y=0;y<height;++y) for (int x=0;x<width;++x)
        image.at<unsigned char>(y,x)=bits[y*stride+x/64]&(1ULL<<(x%64))?255:0;
    return image;
}
inline float similarity(const cv::Mat& ink,const cv::Mat& pattern,double aspect_tolerance=.025) {
    if (ink.rows<6 || ink.cols<2) return 0;
    const double aspect=double(pattern.cols)/pattern.rows;
    if (std::abs(ink.cols-aspect*ink.rows)>std::max(3.,aspect*1.5+ink.cols*aspect_tolerance)) return 0;
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
}
