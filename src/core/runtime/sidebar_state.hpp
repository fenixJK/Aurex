#pragma once
#include "tasks.hpp"
#include "game/detection/state.hpp"
#include <set>

namespace hive::automation {
// Closed is a UI state, not a periodically expiring permission. Share its
// invalidation boundary across nested workflows; click targets still need
// fresh observations in the workflow that uses them.
class SidebarState {
    Time after_{};
    std::set<std::string> pending_;
public:
    void start(Time now) { after_=std::max(after_,now); }
    void invalidate(Time now) { after_=std::max(after_,now+Time(1)); pending_.clear(); }
    static bool affects_ui(const std::vector<Action>& actions) {
        for (const auto& action:actions) {
            const bool changes_ui=action.changes_menu || (action.tap && menu_key(*action.tap))
                || std::any_of(action.held.begin(),action.held.end(),menu_key);
            if (changes_ui) return true;
        }
        return false;
    }
    void submitted(const std::string& id) { pending_.insert(id); }
    void changed(Time finished) { if (finished>=Time{}) after_=std::max(after_,finished+Time(1)); }
    void cancelled(Time now) { if (!pending_.empty()) invalidate(now); }
    bool pending() const { return !pending_.empty(); }
    Time minimum(Time requested) const { return std::max(after_,requested); }
    void completed(const TaskResult& result) {
        if (pending_.erase(result.id)) after_=std::max(after_,result.finished+Time(1));
    }
    bool closed(const ObservationContext& c,const game::State& state,Time minimum) const {
        const auto& panel=state.open_menu;
        if (!pending_.empty() || panel.knowledge!=Knowledge::absent
            || !panel.usable(c,Time::max(),this->minimum(minimum))) return false;
        // A slow sidebar reader can reuse its last closed result while the game
        // is still observable. A stopped capture cannot authorize more input.
        return panel.usable(c,Time(1500))
            || (state.game_hud.has_value(c,Time(2000)) && state.game_hud.value);
    }
};
}
