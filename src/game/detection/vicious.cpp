#include "vicious.hpp"
#include "vicious_glyphs.hpp"
#include <algorithm>
#include <opencv2/imgproc.hpp>

namespace hive::game {
namespace {
constexpr Rect chat_region{.55,0,.45,.45};
std::array<unsigned,2> notifications(const DetectionInput& input) {
    const auto& f=input.frame;
    const double scale=f.width/(f.region.width*f.window.bounds.width);
    const double full_width=f.width/f.region.width,full_height=f.height/f.region.height;
    const double width=std::floor(std::min(475.,f.window.bounds.width/2.5))*scale;
    const double height=std::floor(std::min(334.,f.window.bounds.height/3.464))*scale;
    const int left=std::max(0,int(std::lround(full_width-8*scale-width-f.region.x*full_width)));
    const int top=std::max(0,int(std::lround(40*scale-f.region.y*full_height)));
    const int right=std::min(f.width,int(std::lround(full_width-8*scale-f.region.x*full_width)));
    const int bottom=std::min(f.height,top+int(std::lround(height)));
    if (right<=left || bottom<=top) return {};
    const cv::Mat pixels(f.height,f.width,CV_8UC4,const_cast<std::uint8_t*>(f.rgba.data()));
    cv::Mat chat;
    // Work in window points: Retina capture does not quadruple the search cost.
    cv::resize(pixels(cv::Rect(left,top,right-left,bottom-top)),chat,
        cv::Size(std::max(1,int(std::lround((right-left)/scale))),std::max(1,int(std::lround((bottom-top)/scale)))),0,0,cv::INTER_AREA);
    std::array<std::vector<int>,2> rows;
    for (const auto& glyph:detail::vicious_glyphs) {
        const auto near=[](const cv::Vec4b& pixel,std::uint32_t rgb) {
            return pixel[3]>=240 && std::abs(int(pixel[0])-int(rgb>>16))<=5
                && std::abs(int(pixel[1])-int((rgb>>8)&255))<=5 && std::abs(int(pixel[2])-int(rgb&255))<=5;
        };
        for (int y=0;y+glyph.height<=chat.rows;++y) {
            if (input.cancelled && input.cancelled()) return {};
            for (int x=0;x+glyph.width<=chat.cols;++x) {
                if (!near(chat.at<cv::Vec4b>(y,x),glyph.rgb[0])) continue;
                bool match=true;
                for (int dy=0;match && dy<glyph.height;++dy) for (int dx=0;dx<glyph.width;++dx)
                    if (!near(chat.at<cv::Vec4b>(y+dy,x+dx),glyph.rgb[dy*glyph.width+dx])) { match=false; break; }
                if (match) rows[glyph.defeated].push_back(y);
            }
        }
    }
    std::array<unsigned,2> counts{};
    for (unsigned k=0;k<2;++k) {
        auto& hits=rows[k]; std::sort(hits.begin(),hits.end());
        int previous=-100;
        for (const auto y:hits) if (y-previous>12) { ++counts[k]; previous=y; }
    }
    return counts;
}
class ViciousDetector final : public Detector {
    mutable Observation<ViciousSignals> previous_;
    mutable std::array<unsigned,2> counts_{},candidate_{};
    mutable Time candidate_at_{};
public:
    Rect capture_region() const override { return chat_region; }
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        auto result=std::make_shared<ViciousDetection>();
        const auto& f=input.frame;
        if (!f.valid() || f.width>16000 || f.height>16000) return result;
        const ObservationContext context{f.window,input.generation,f.captured};
        const auto counts=notifications(input);
        if (input.cancelled && input.cancelled()) return result;
        ViciousSignals events;
        if (!previous_.has_value(context,Time(5000))) {
            // Joining a server or opening this detector must not replay old chat.
            counts_=candidate_=counts; candidate_at_=f.captured;
        } else {
            events=previous_.value;
            if (candidate_!=counts) { candidate_=counts; candidate_at_=f.captured; }
            else if (f.captured>candidate_at_) {
                // Require distinct frames, and count a notification once as it arrives.
                if (counts[0]>counts_[0] && !events.engaged) events.engaged=candidate_at_;
                if (counts[1]>counts_[1] && !events.defeated) events.defeated=candidate_at_;
                counts_=counts;
            }
        }
        for (auto* event:{&events.engaged,&events.defeated})
            if (*event && f.captured-**event>std::chrono::minutes(3)) event->reset();
        result->events={Knowledge::present,events,f.captured,f.window,input.generation};
        previous_=result->events;
        return result;
    }
};
}
std::shared_ptr<const Detector> vicious_detector() { return std::make_shared<ViciousDetector>(); }
Observation<ViciousSignals> read_vicious(const DetectionSource& source,const ObservationContext& context) {
    const auto packet=source.read();
    if (!packet || !packet->error.empty()) return {};
    const Observation<bool> envelope{Knowledge::present,true,packet->context.now,packet->context.window,packet->context.generation};
    if (!envelope.has_value(context,Time(2000))) return {};
    const auto result=std::dynamic_pointer_cast<const ViciousDetection>(packet->value);
    return result && result->events.has_value(context,Time(2000))?result->events:Observation<ViciousSignals>{};
}
}
