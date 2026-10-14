#include "hive_setup.hpp"

namespace hive::automation {
bool WhirligigCooldown::reserve(Time now) {
    std::lock_guard lock(mutex_);
    if (attempted_ && now>=*attempted_ && now-*attempted_<std::chrono::minutes(3)) return false;
    attempted_=now; return true;
}
namespace {
class Whirligig final : public Program {
    unsigned slot_,phase_{};
    Time pressed_{};
    std::shared_ptr<WhirligigCooldown> cooldown_;
    std::shared_ptr<const game::LandmarkSource> proximity_;
    ProgramFactory prepare_,return_method_;
public:
    Whirligig(unsigned slot,std::shared_ptr<WhirligigCooldown> cooldown,
        std::shared_ptr<const game::LandmarkSource> proximity,ProgramFactory prepare,ProgramFactory return_method)
        :slot_(slot),cooldown_(std::move(cooldown)),proximity_(std::move(proximity)),prepare_(std::move(prepare)),return_method_(std::move(return_method)) {
        if (slot_<1 || slot_>7 || !cooldown_ || !proximity_ || !prepare_ || !return_method_)
            throw std::invalid_argument("Whirligig return needs a hotbar slot and hive setup");
    }
    RunConfig config() const override { return {Time(310000),28,{},true,true}; }
    std::string activity_label() const override { return "Returning to hive"; }
    Step next(const View& view) override {
        if (!phase_) {
            if (!cooldown_->reserve(view.context.now)) { phase_=3; return RunProgram{return_method_}; }
            phase_=1;
            return Hold{{static_cast<Key>(static_cast<int>(Key::hotbar_1)+slot_-1)},Time(50)};
        }
        if (phase_==2) {
            const auto at_hive=proximity_->read(view.context);
            if (at_hive.has_value(view.context,Time(2000),pressed_) && at_hive.value) {
                phase_=3; return RunProgram{prepare_};
            }
            if (view.context.now-pressed_>=Time(5000)) { phase_=3; return RunProgram{return_method_}; }
            return Wait{Time(50),"Returning with Whirligig"};
        }
        return Done{"Returned to hive"};
    }
    void step_completed(const View& view) override {
        if (phase_==1) { pressed_=view.context.now; phase_=2; }
    }
};
}
std::unique_ptr<Program> whirligig_to_hive(unsigned slot,std::shared_ptr<WhirligigCooldown> cooldown,
    std::shared_ptr<const game::LandmarkSource> proximity,ProgramFactory prepare,ProgramFactory return_method) {
    return std::make_unique<Whirligig>(slot,std::move(cooldown),std::move(proximity),std::move(prepare),std::move(return_method));
}
}
