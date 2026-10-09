#include "movement_setup.hpp"

namespace hive::automation {
namespace {
class PrepareMovement final : public Program {
    Time began_{Time(-1)};
public:
    RunConfig config() const override { return {Time(10000),28,{},true,true}; }
    Step next(const View& view) override {
        if (began_<Time{}) began_=view.context.now;
        const auto speed=view.observations?view.observations->movement_speed:Observation<double>{};
        if (!speed.has_value(view.context,Time(1500),began_) && view.context.now-began_<Time(1500))
            return Wait{Time(50),"Reading movement buffs"};
        return Done{"Movement setup ready"};
    }
};
}
std::unique_ptr<Program> prepare_movement() { return std::make_unique<PrepareMovement>(); }
}
