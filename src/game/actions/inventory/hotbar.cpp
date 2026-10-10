#include "hotbar.hpp"
#include <algorithm>

namespace hive::automation {
bool attach_hotbar(std::span<Action> actions,HotbarSource& source,const HotbarContext& context) {
    if (actions.empty()) return false;
    const auto compatible=[&](const Action& action) {
        const bool aimed_attack=context.activity==WorkActivity::fighting && std::find(action.held.begin(),action.held.end(),Key::primary)!=action.held.end();
        return (!action.pointer || aimed_attack) && !action.pointer_to && !action.scroll && !action.tap && !action.concurrent_pointer
            && std::all_of(action.held.begin(),action.held.end(),[](Key key) {
                return (key>=Key::forward && key<=Key::right) || key==Key::primary;
            });
    };
    if (!std::all_of(actions.begin(),actions.end(),compatible) || actions.front().duration<Time(50)) return false;
    const auto slot=source.take(context);
    if (!slot || slot>7) return false;
    actions.front().tap=Key(int(Key::hotbar_1)+slot-1);
    actions.front().tap_duration=Time(50);
    return true;
}
}
