#include "buff_stack.hpp"
#include "buff_counters.hpp"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace hive::game::detail {
std::optional<unsigned> read_buff_stack(const DetectionInput& input,Rect cell,double scale,unsigned maximum) {
    if (!input.frame.valid() || scale<.5 || scale>4 || (input.cancelled && input.cancelled())) return {};
    const auto& frame=input.frame;
    const int left=std::max(0,int(std::floor(cell.x-3*scale))),top=std::max(0,int(std::floor(cell.y+18*scale)));
    const int right=std::min(frame.width,int(std::ceil(cell.x+41*scale))),bottom=std::min(frame.height,int(std::ceil(cell.y+42*scale)));
    if (right<=left || bottom<=top) return {};
    const cv::Mat rgba(frame.height,frame.width,CV_8UC4,const_cast<std::uint8_t*>(frame.rgba.data()));
    cv::Mat normalized;
    cv::resize(rgba(cv::Rect(left,top,right-left,bottom-top)),normalized,cv::Size(),1/scale,1/scale,cv::INTER_AREA);
    cv::Mat ink(normalized.rows,normalized.cols,CV_32F);
    for (int y=0;y<normalized.rows;++y) for (int x=0;x<normalized.cols;++x) {
        const auto p=normalized.at<cv::Vec4b>(y,x);
        const int lo=std::min({p[0],p[1],p[2]}),hi=std::max({p[0],p[1],p[2]});
        ink.at<float>(y,x)=p[3]>=240 && hi-lo<50?std::clamp((lo-110)/133.f,0.f,1.f):0;
    }
    static const auto templates=[] {
        std::array<cv::Mat,buff_counters.size()> result;
        for (std::size_t i=0;i<result.size();++i) {
            const auto& counter=buff_counters[i]; auto& image=result[i]; image.create(counter.height,counter.width,CV_32F);
            for (int y=0;y<image.rows;++y) for (int x=0;x<image.cols;++x) image.at<float>(y,x)=((counter.rows[y]>>(x*2))&3)/3.f;
        }
        return result;
    }();
    double best=1,second=1; unsigned value=0;
    for (std::size_t i=0;i<templates.size();++i) {
        if (input.cancelled && input.cancelled()) return {};
        const auto& counter=buff_counters[i];
        if (counter.width>ink.cols || counter.height>ink.rows) continue;
        cv::Mat scores; cv::matchTemplate(ink,templates[i],scores,cv::TM_SQDIFF_NORMED);
        double error; cv::Point at; cv::minMaxLoc(scores,&error,nullptr,&at);
        const int end=at.x+counter.width;
        const int edge=std::min(ink.cols,int(std::floor((cell.x+cell.width-left)/scale)));
        if (end<edge && cv::sum(ink(cv::Rect(end,at.y+2,edge-end,counter.height-4)))[0]>3) continue;
        if (error<best) { second=best; best=error; value=counter.value; }
        else second=std::min(second,error);
    }
    if (value && best<.7 && second-best>.035) return value<=maximum?std::optional<unsigned>(value):std::nullopt;
    if (best>=.8 && ink.cols>=10 && ink.rows>=templates.front().rows) {
        cv::Mat scores;
        cv::matchTemplate(ink,templates.front()(cv::Rect(0,0,10,templates.front().rows)),scores,cv::TM_SQDIFF_NORMED);
        double marker; cv::minMaxLoc(scores,&marker);
        if (marker>.75) return 1;
    }
    return {};
}
}
