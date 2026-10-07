#include "hive/automation/recenter.hpp"
#include <algorithm>

namespace hive::automation {
namespace {
class Recenter final : public Program {
    std::shared_ptr<const game::ScreenReferenceSource> source_;
    double speed_;
    std::optional<Observation<Point>> previous_;
    Time minimum_{},last_{Time(-1)};
    bool moving_{},pulse_{};
    bool started_{};
    double best_error_{1.}; unsigned stagnant_{},pulses_{};
    std::vector<Key> previous_keys_;
public:
    Recenter(std::shared_ptr<const game::ScreenReferenceSource> source,double speed):source_(std::move(source)),speed_(speed) {
        if (!source_ || !std::isfinite(speed_) || speed_<8 || speed_>200) throw std::invalid_argument("Invalid screen recenter services");
    }
    RunConfig config() const override { return {Time(6500),speed_,{},true,true}; }
    Step next(const View& view) override {
        if (!started_) { minimum_=view.context.now; started_=true; }
        if (view.elapsed>=Time(6000)) return Done{"Reference correction limit reached"};
        if (!moving_ && view.elapsed>=Time(750)) return Done{"Reference was not confirmed in time"};
        const auto reading=source_->read(view.context);
        if (!reading.has_value(view.context,Time(750))) {
            if (moving_ || view.elapsed>=Time(750)) return Done{"Reference unavailable"};
            return Wait{Time(25),"Reading position reference"};
        }
        if (reading.captured<minimum_) return Wait{Time(25),"Waiting for a post-movement reference"};
        const auto p=reading.value;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x<.2 || p.x>.8 || p.y<.2 || p.y>.8)
            return Done{"Reference outside correction area"};
        if (reading.captured<=last_) return Wait{Time(25),"Waiting for a new reference frame"};
        last_=reading.captured;
        if (previous_ && std::hypot(p.x-previous_->value.x,p.y-previous_->value.y)>.15) return Done{"Position reference changed"};
        previous_=reading;
        std::vector<Key> keys;
        if (p.x<1/2.14) keys.push_back(Key::left); else if (p.x>1/1.88) keys.push_back(Key::right);
        if (p.y<1/2.14) keys.push_back(Key::forward); else if (p.y>1/1.88) keys.push_back(Key::backward);
        if (keys.empty()) return Done{"Position reference centered"};
        const double error=std::hypot(p.x-.5,p.y-.5);
        if (moving_) {
            if (error<best_error_-.003) stagnant_=0; else ++stagnant_;
            if (stagnant_>=3 || pulses_>=12) return Done{"Reference correction made insufficient progress"};
            for (const auto key:keys) {
                const auto opposite=key==Key::left?Key::right:key==Key::right?Key::left:key==Key::forward?Key::backward:Key::forward;
                if (std::find(previous_keys_.begin(),previous_keys_.end(),opposite)!=previous_keys_.end())
                    return Done{"Reference correction crossed its target"};
            }
        }
        best_error_=std::min(best_error_,error); previous_keys_=keys; ++pulses_;
        moving_=pulse_=true;
        return Hold{std::move(keys),Time(static_cast<Time::rep>(std::clamp(1000./speed_,5.,100.)))};
    }
    void step_completed(const View& view) override {
        if (pulse_) { minimum_=view.context.now+Time(1); pulse_=false; }
    }
};
}
std::unique_ptr<Program> recenter(std::shared_ptr<const game::ScreenReferenceSource> source,double speed) {
    return std::make_unique<Recenter>(std::move(source),speed);
}
}
