#include "hive/automation/shift_lock.hpp"
#include "hive/game/detection/menus.hpp"
namespace hive::automation {
namespace {
class ShiftLock final : public Program {
    bool enabled_,sent_{},toggle_pending_{},parked_{};
    Time last_{Time(-1)},minimum_{};
public:
    explicit ShiftLock(bool enabled):enabled_(enabled) {}
    RunConfig config() const override { return {Time(5000),28,{},true,true}; }
    Step next(const View& view) override {
        if (parked_) return Done{"Shift lock confirmed"};
        if (!view.observations) return Wait{Time(25),"Reading shift lock"};
        const auto& reading=view.observations->shift_lock;
        if (!reading.has_value(view.context,Time(1500),minimum_) || reading.captured<=last_)
            return Wait{Time(25),"Reading shift lock"};
        last_=reading.captured;
        if (reading.value==enabled_) {
            if (!enabled_ && sent_) {
                parked_=true; return Hold{{},Time(1),game::pointer_rest(view.context,*view.observations)};
            }
            return Done{"Shift lock confirmed"};
        }
        if (sent_) return Wait{Time(25),"Waiting for shift lock to change"};
        sent_=toggle_pending_=true; return Hold{{Key::shift_lock},Time(50)};
    }
    void step_completed(const View& view) override { if (toggle_pending_) { minimum_=view.context.now+Time(1); toggle_pending_=false; } }
};
}
std::unique_ptr<Program> set_shift_lock(bool enabled) { return std::make_unique<ShiftLock>(enabled); }
}
