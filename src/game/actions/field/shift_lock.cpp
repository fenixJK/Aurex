#include "shift_lock.hpp"
namespace hive::automation {
namespace {
class ShiftLock final : public Program {
    bool enabled_,sent_{},toggle_pending_{},parked_{};
    Time last_{Time(-1)},minimum_{};
public:
    explicit ShiftLock(bool enabled):enabled_(enabled) {}
    RunConfig config() const override { return {Time(5000),28,{},true,true}; }
    game::ObservationNeeds observation_needs() const override { return {game::ObservationKind::shift_lock}; }
    Step next(const View& view) override {
        if (parked_) return Done{"Shift lock confirmed"};
        if (!view.observations) return Wait{Time(25),"Reading shift lock"};
        const auto& reading=view.observations->shift_lock;
        if (!reading.has_value(view.context,Time(1500),minimum_) || reading.captured<=last_)
            return Wait{Time(25),"Reading shift lock"};
        last_=reading.captured;
        if (reading.value==enabled_) {
            if (!enabled_) {
                // Camera setup can still be in first person after unlocking.
                // Keep its neutral origin separate from the normal UI rest.
                parked_=true; return Hold{{},Time(1),Point{.5,.5}};
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
