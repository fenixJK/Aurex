#include "passive_counters.hpp"
#include "passive_references.hpp"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>

namespace hive::game {
namespace {
struct Definition { const char* name; const std::uint8_t* art; unsigned maximum; };
constexpr std::array definitions{Definition{"Star Saw",detail::saw_art.data(),2}};
struct Match { double score{}; cv::Point point; int diameter{}; };

std::optional<unsigned> counter(const cv::Mat& rgba,const Match& icon,unsigned maximum) {
    const double scale=icon.diameter/32.;
    const int left=std::lround(icon.point.x+8*scale),top=std::lround(icon.point.y+4*scale);
    const cv::Rect box(left,top,int(std::ceil(21*scale)),int(std::ceil(19*scale)));
    if((box & cv::Rect(0,0,rgba.cols,rgba.rows))!=box)return {};
    cv::Mat ink(box.height,box.width,CV_32F);
    for(int y=0;y<box.height;++y)for(int x=0;x<box.width;++x) {
        const auto p=rgba.at<cv::Vec4b>(top+y,left+x);
        const int low=std::min({p[0],p[1],p[2]}),high=std::max({p[0],p[1],p[2]});
        ink.at<float>(y,x)=std::clamp((low-100-(high-low)*.5)/155.,0.,1.);
    }
    std::vector<double> scores(maximum+1,-1);
    for(const auto& digit:detail::passive_digits) {
        if(digit.value>maximum)continue;
        const cv::Mat original(digit.height,digit.width,CV_8U,const_cast<std::uint8_t*>(digit.pixels.data()));
        for(int height=std::max(6,int(icon.diameter*.3));height<=icon.diameter*.58;++height) {
            const int width=std::lround(double(digit.width)*height/digit.height);
            if(width>ink.cols || height>ink.rows)continue;
            cv::Mat reference,quality;
            cv::resize(original,reference,{width,height},0,0,cv::INTER_AREA);
            reference.convertTo(reference,CV_32F,1./255);
            cv::matchTemplate(ink,reference,quality,cv::TM_CCOEFF_NORMED);
            double score;cv::minMaxLoc(quality,nullptr,&score);
            scores[digit.value]=std::max(scores[digit.value],score);
        }
    }
    const auto best=std::max_element(scores.begin(),scores.end());
    const auto value=unsigned(best-scores.begin());const double quality=*best;
    *best=-1;
    if(quality<.60 || quality-*std::max_element(scores.begin(),scores.end())<.12)return {};
    return value;
}
class Counters final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& frame=input.frame;
        if(!frame.valid() || !input.generation || frame.region!=Rect{0,0,1,1})return {};
        auto result=std::make_shared<PassiveCounters>();
        const cv::Mat rgba(frame.height,frame.width,CV_8UC4,const_cast<std::uint8_t*>(frame.rgba.data()));
        const int top=frame.height*3/4;
        cv::Mat gray;cv::cvtColor(rgba(cv::Rect(0,top,frame.width,frame.height-top)),gray,cv::COLOR_RGBA2GRAY);
        const double reduction=std::min(1.,960./frame.width);
        cv::Mat small;cv::resize(gray,small,{},reduction,reduction,cv::INTER_AREA);
        for(const auto& definition:definitions) {
            const cv::Mat original(13,24,CV_8U,const_cast<std::uint8_t*>(definition.art));
            cv::Mat mask(13,24,CV_8U,cv::Scalar(255));
            // The numeral overlaps the lower-right artwork. It must not
            // influence identity or the scale used to read that numeral.
            mask(cv::Rect(14,7,10,6)).setTo(0);
            Match best;
            // HUD scale varies independently of window size. Search artwork,
            // never a fixed slot: equipment changes the order of these circles.
            for(int diameter=16;diameter<=std::min(80,frame.height/8);diameter+=4) {
                if(input.cancelled && input.cancelled())return {};
                cv::Mat reference,quality;
                cv::resize(original,reference,{int(std::lround(24.*diameter/32*reduction)),int(std::lround(13.*diameter/32*reduction))},0,0,cv::INTER_AREA);
                cv::matchTemplate(small,reference,quality,cv::TM_CCOEFF_NORMED);
                double score;cv::Point point;cv::minMaxLoc(quality,nullptr,&score,nullptr,&point);
                if(score<.6)continue;
                const cv::Rect search=cv::Rect(int(point.x/reduction)-5,int(point.y/reduction)-5,diameter+14,diameter+14)
                    & cv::Rect(0,0,gray.cols,gray.rows);
                for(int size=std::max(16,diameter-3);size<=diameter+3;++size) {
                    cv::Mat stencil;
                    const cv::Size dimensions(int(std::lround(24.*size/32)),int(std::lround(13.*size/32)));
                    if(dimensions.width>search.width || dimensions.height>search.height)continue;
                    cv::resize(original,reference,dimensions,0,0,cv::INTER_AREA);
                    cv::resize(mask,stencil,dimensions,0,0,cv::INTER_NEAREST);
                    cv::matchTemplate(gray(search),reference,quality,cv::TM_CCOEFF_NORMED,stencil);
                    for(int y=0;y<quality.rows;++y)for(int x=0;x<quality.cols;++x)
                        if(const double value=quality.at<float>(y,x);std::isfinite(value) && value>best.score)
                            best={value,{search.x+x,search.y+y+top},size};
                }
            }
            if(best.score>=.78)result->counters.push_back({definition.name,counter(rgba,best,definition.maximum)});
        }
        return result;
    }
};
}
std::shared_ptr<const Detector> passive_counter_detector() { return std::make_shared<Counters>(); }
}
