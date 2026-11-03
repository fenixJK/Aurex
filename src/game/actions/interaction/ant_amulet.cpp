#include "ant_amulet.hpp"
#include "game/detection/dialog.hpp"
#include "game/detection/menus.hpp"

namespace hive::automation {
namespace {
class KeepAntAmulet final : public Program {
    Time started_{Time(-1)},minimum_{},retry_at_{},frame_{Time(-1)};
    bool issued_{};
public:
    RunConfig config() const override { return {Time(12000),28,{},true,true}; }
    std::vector<game::FixedLabelQuery> fixed_labels() const override { return game::ant_amulet_labels(); }
    // Losing the Ant Challenge can itself cause respawn while its reward opens.
    bool expects_respawn() const noexcept override { return true; }
    Step next(const View& view) override {
        const auto& c=view.context;
        if (started_<Time{}) { started_=c.now; minimum_=c.now+Time(1); }
        if (view.observations) {
            const auto& state=*view.observations;
            const auto buttons=game::read_ant_amulet(c,state);
            if (buttons.usable(c,Time(2000),minimum_) && buttons.captured>frame_) {
                if (buttons.knowledge==Knowledge::absent && state.game_hud.has_value(c,Time(2000),minimum_) && state.game_hud.value)
                    return Done{"Ant Amulet choice cleared"};
                // A delivered click may be dropped by a lagging client. Only
                // retry the freshly recognized Keep Old button after response time.
                if (buttons.knowledge==Knowledge::present && buttons.captured>=retry_at_) {
                    frame_=buttons.captured; issued_=true;
                    return UseControl{{Knowledge::present,buttons.value.yes,buttons.captured,c.window,c.generation},
                        0,"Keep existing Ant Amulet",game::pointer_rest(c,state)};
                }
            }
        }
        if (c.now-started_>=Time(10000)) throw ObservationUnavailable("Could not confirm the Ant Amulet choice closed");
        return Wait{Time(100),"Checking Ant Amulet choice"};
    }
    void step_completed(const View& view) override {
        if (!issued_) return;
        issued_=false; minimum_=view.context.now+Time(1); retry_at_=view.context.now+Time(750);
    }
};
}
std::unique_ptr<Program> keep_ant_amulet() { return std::make_unique<KeepAntAmulet>(); }
}
