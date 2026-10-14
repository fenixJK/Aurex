#include "recenter.hpp"
#include <algorithm>
#include <array>

namespace hive::automation {
namespace {
class Recenter final : public Program {
    std::shared_ptr<const game::ScreenReferenceSource> source_;
    double speed_;
    std::optional<Point> previous_,expected_;
    std::array<double,2> scale_{};
    std::array<double,2> distance_{};
    Time minimum_{},last_{Time(-1)};
    bool moving_{};
public:
    Recenter(std::shared_ptr<const game::ScreenReferenceSource> source,double speed):source_(std::move(source)),speed_(speed) {
        if (!source_ || !std::isfinite(speed_) || speed_<8 || speed_>200) throw std::invalid_argument("Invalid screen recenter services");
    }
    RunConfig config() const override { return {Time(6500),speed_,{},true,true}; }
    Step next(const View& view) override {
        if (view.elapsed>=Time(6000)) return Done{"Reference correction limit reached"};
        const auto reading=source_->read(view.context,expected_);
        if (!reading.has_value(view.context,Time(750),minimum_) || reading.captured<=last_) {
            if (previous_ ? view.context.now-minimum_>=Time(750) : view.elapsed>=Time(750))
                return Done{"Reference unavailable"};
            return Wait{Time(25),"Reading position reference"};
        }
        const auto p=reading.value;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x<.2 || p.x>.8 || p.y<.2 || p.y>.8)
            return Done{"Reference outside correction area"};
        last_=reading.captured;
        const std::array error{p.x-.5,p.y-.5};
        if (std::abs(error[0])<=.03 && std::abs(error[1])<=.03) return Done{"Position reference centered"};
        if (previous_) {
            if (std::hypot(p.x-.5,p.y-.5)>std::hypot(previous_->x-.5,previous_->y-.5)+.005)
                return Done{"Reference correction made insufficient progress"};
            const std::array shift{p.x-previous_->x,p.y-previous_->y};
            for (unsigned axis=0;axis<2;++axis) if (distance_[axis]!=0) {
                const double measured=-shift[axis]/distance_[axis];
                if (measured<.002 || measured>.15) return Done{"Reference correction direction changed"};
                scale_[axis]=measured;
            }
        }
        distance_={};
        for (unsigned axis=0;axis<2;++axis) if (std::abs(error[axis])>.03)
            distance_[axis]=std::copysign(scale_[axis]>0?std::clamp((std::abs(error[axis])-.01)/scale_[axis],.1,1.5):.25,error[axis]);
        const bool diagonal=distance_[0]!=0 && distance_[1]!=0;
        if (diagonal) {
            const double along=std::min({std::abs(distance_[0]),std::abs(distance_[1]),1.5/std::sqrt(2.)});
            for (auto& distance:distance_) distance=std::copysign(along,distance);
        }
        const auto horizontal=distance_[0]>0?Key::right:Key::left,vertical=distance_[1]>0?Key::backward:Key::forward;
        previous_=p; expected_=Point{p.x-distance_[0]*scale_[0],p.y-distance_[1]*scale_[1]}; moving_=true;
        return Walk{{std::hypot(distance_[0],distance_[1]),distance_[0]!=0?horizontal:vertical,diagonal?std::optional(vertical):std::nullopt}};
    }
    void step_completed(const View& view) override {
        if (moving_) { minimum_=view.context.now+Time(1); moving_=false; }
    }
};
}
std::unique_ptr<Program> recenter(std::shared_ptr<const game::ScreenReferenceSource> source,double speed) {
    return std::make_unique<Recenter>(std::move(source),speed);
}
}
