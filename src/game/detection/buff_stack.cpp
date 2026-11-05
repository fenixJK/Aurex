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
    // Match the outlined lettering in luminance. Buff timer shading and scaled
    // captures tint the white strokes; a near-white RGB mask can erase them.
    cv::Mat gray; cv::cvtColor(normalized,gray,cv::COLOR_RGBA2GRAY);
    gray.convertTo(gray,CV_32F,1./255);
    static const auto masks=[] {
        std::array<cv::Mat,buff_counters.size()> result;
        for (std::size_t i=0;i<result.size();++i) {
            cv::Mat outline; cv::dilate(templates[i],outline,cv::Mat::ones(3,3,CV_8U));
            cv::compare(outline,.1,result[i],cv::CMP_GT);
        }
        return result;
    }();
    double best=0,second=0; unsigned value=0;
    const int edge=int(std::round((cell.x+cell.width-left)/scale));
    for (std::size_t i=0;i<templates.size();++i) {
        if (input.cancelled && input.cancelled()) return {};
        const auto& counter=buff_counters[i];
        if (counter.width>gray.cols || counter.height>gray.rows) continue;
        cv::Mat scores; cv::matchTemplate(gray,templates[i],scores,cv::TM_CCOEFF_NORMED,masks[i]);
        double score=0;
        // Stack captions end at the cell's right edge. This also prevents
        // recognizing a prefix of a larger count or the neighbouring icon.
        for (int y=0;y<scores.rows;++y) for (int x=0;x<scores.cols;++x) {
            if (std::abs(x+counter.width-edge)>3) continue;
            const float candidate=scores.at<float>(y,x);
            if (std::isfinite(candidate)) score=std::max(score,double(candidate));
        }
        if (score>best) { second=best; best=score; value=counter.value; }
        else second=std::max(second,score);
    }
    if (value && best>.6 && best-second>.025) return value<=maximum?std::optional<unsigned>(value):std::nullopt;
    // A weak number match is not proof of a single stack. Require the x marker
    // to be absent in both the color and outline views before using one.
    if (best<.45 && ink.cols>=10 && ink.rows>=templates.front().rows) {
        cv::Mat scores;
        const auto marker=templates.front()(cv::Rect(0,0,10,templates.front().rows));
        cv::matchTemplate(ink,marker,scores,cv::TM_SQDIFF_NORMED);
        double error; cv::minMaxLoc(scores,&error);
        cv::matchTemplate(gray,marker,scores,cv::TM_CCOEFF_NORMED,masks.front()(cv::Rect(0,0,10,marker.rows)));
        double outline=0;
        for (int y=0;y<scores.rows;++y) for (int x=0;x<scores.cols;++x)
            if (const auto score=scores.at<float>(y,x);std::isfinite(score)) outline=std::max(outline,double(score));
        if (error>.75 && outline<.45) return 1;
    }
    return {};
}
}
