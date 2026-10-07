#include "hive/automation/movement_setup.hpp"
#include "hive/game/detection/menus.hpp"
#include <algorithm>

namespace hive::automation {
namespace {
class PrepareMovement final : public Program {
    Time minimum_{},began_{Time(-1)};
    unsigned attempts_{};
    bool clicked_{};
public:
    RunConfig config() const override { return {Time(10000),28,{},true,true}; }
    Step next(const View& view) override {
        if (began_<Time{}) began_=minimum_=view.context.now;
        if (view.context.now<minimum_) return Wait{Time(50),"Closing game menu"};
        const bool quests_open=view.observations && view.observations->menus.has_value(view.context,Time(1500),minimum_)
            && std::any_of(view.observations->menus.value.begin(),view.observations->menus.value.end(),[](const auto& menu) {
                return menu.tab==game::MenuTab::quests && menu.selected==true;
            });
        auto control=view.observations?view.observations->open_menu:Observation<Point>{};
        if (quests_open || !control.has_value(view.context,Time(1500),minimum_)) {
            const auto speed=view.observations?view.observations->movement_speed:Observation<double>{};
            if (!speed.has_value(view.context,Time(1500),began_) && view.context.now-began_<Time(1500))
                return Wait{Time(50),"Reading movement buffs"};
            return Done{"Movement setup ready"};
        }
        if (++attempts_>3) throw std::runtime_error("Game menu did not close after three clicks");
        clicked_=true;
        return UseControl{control,0,"Closing game menu",game::pointer_rest(view.context,view.observations?*view.observations:game::State{})};
    }
    void step_completed(const View& view) override {
        if (clicked_) { clicked_=false; minimum_=view.context.now+Time(250); }
    }
};
}
std::unique_ptr<Program> prepare_movement() { return std::make_unique<PrepareMovement>(); }
}
