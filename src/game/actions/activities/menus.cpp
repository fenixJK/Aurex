#include "core/runtime/runner.hpp"
#include "game/detection/menus.hpp"
#include <algorithm>

namespace hive::automation {
std::optional<game::MenuTab> Runner::movement_menu() const {
    if (auto menu=program_->movement_menu()) return menu;
    return parent_menu_?parent_menu_():std::nullopt;
}
bool Runner::prepares_menu() const {
    if (!step_) return false;
    // Reset owns the keyboard until it finishes. Never start sidebar clicks
    // over its menu/confirmation, including from a parent workflow.
    if (expects_respawn()) return false;
    if (!permissions_.contains(Permission::interact) || checkpoint_ || navigation_ || sprinkler_) return false;
    if (const auto* travel=std::get_if<Navigate>(&*step_); travel && travel->inventory_search) return false;
    const auto* hold=std::get_if<Hold>(&*step_);
    const bool walking_hold=hold && std::any_of(hold->keys.begin(),hold->keys.end(),[](auto key) {
        return key==Key::forward || key==Key::backward || key==Key::left || key==Key::right;
    });
    const bool moving=walking_hold || std::holds_alternative<Walk>(*step_) || std::holds_alternative<Navigate>(*step_)
        || std::holds_alternative<MoveCamera>(*step_) || std::holds_alternative<SetHeading>(*step_)
        || std::holds_alternative<FollowPath>(*step_)
        || std::holds_alternative<ApproachLandmark>(*step_) || std::holds_alternative<ApproachPrompt>(*step_);
    return moving;
}
bool Runner::waiting_for_menu() const {
    return menu_waiting_ || (child_ && child_->waiting_for_menu())
        || (pattern_ && pattern_->waiting_for_menu()) || (recovery_ && recovery_->waiting_for_menu());
}
bool Runner::prepare_menu(const ObservationContext& c,const game::State& s) {
    menu_waiting_=false;
    if (!prepares_menu()) return false;
    if (!menu_pending_.empty()) {
        for (const auto& result:results(scope_)) if (result.id==menu_pending_) {
            menu_pending_.clear();
            if (result.outcome!=TaskOutcome::executed) throw std::runtime_error("Menu cleanup interrupted");
            // Confirm from the first post-input frame. Spacing applies only
            // to another click, never to a successful close/open observation.
            menu_after_=result.finished+Time(1);
            menu_retry_at_=result.finished+menu_retry_delay_;
        }
        if (!menu_pending_.empty()) { menu_waiting_=true; return true; }
    }
    std::optional<Point> target;
    // Camera keys such as Page Up can scroll an open quest/inventory panel.
    // Tracking menus may reopen for movement after the camera adjustment.
    const bool route_camera=std::visit([](const auto& step) {
        using T=std::decay_t<decltype(step)>;
        if constexpr (std::is_same_v<T,FollowPath> || std::is_same_v<T,ApproachLandmark> || std::is_same_v<T,ApproachPrompt>)
            return workflows::uses_camera(step.movement);
        return false;
    },*step_);
    const bool camera=route_camera || std::holds_alternative<MoveCamera>(*step_) || std::holds_alternative<SetHeading>(*step_);
    const auto required=camera?std::optional<game::MenuTab>{}:movement_menu();
    const auto menu_minimum=sidebar_->minimum(menu_after_);
    // Confirm the tab we closed directly; another unreadable icon does not
    // undo a fresh observation that this panel is no longer selected.
    bool closed=false;
    if (!required && closing_menu_ && !sidebar_->pending() && s.menus.has_value(c,Time(1500),menu_minimum))
        for (const auto& menu:s.menus.value) if (menu.tab==*closing_menu_ && menu.selected==false) {
            menu_retry_delay_=menu_retry_at_={}; closing_menu_.reset(); closed=true; break;
        }
    const bool fresh=!sidebar_->pending() && (required?s.menus.has_value(c,Time(1500),menu_minimum):s.open_menu.usable(c,Time(1500),menu_minimum));
    if (!required && sidebar_->closed(c,s,menu_after_)) {
        menu_retry_delay_=menu_retry_at_={}; closing_menu_.reset();
        return false;
    }
    if (!fresh && menu_retry_delay_>Time{}) {
        menu_waiting_=true;
        progress_.message="Waiting for game menu update";
        return true;
    }
    // Missing UI evidence is a local wait, not evidence of a lost route. Keep
    // requesting it without resetting/rejoining or spending the route deadline.
    if (camera && !fresh && !closed) {
        menu_waiting_=true;
        progress_.message="Waiting for a readable game menu view";
        return true;
    }
    if (required) {
        if (!fresh) return false;
        for (const auto& menu:s.menus.value) if (menu.tab==*required) {
            if (menu.selected==true) { menu_retry_delay_=menu_retry_at_={}; return false; }
            if (menu.selected==false) target=menu.center();
            break;
        }
    } else if (fresh && s.open_menu.knowledge==Knowledge::present) target=s.open_menu.value;
    else if (fresh && s.open_menu.knowledge==Knowledge::absent) { menu_retry_delay_=menu_retry_at_={}; closing_menu_.reset(); }
    if (!target) return false;
    const auto point=*target;
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x<0 || point.y<0 || point.x>=1 || point.y>=1)
        throw std::runtime_error("Invalid menu control position");
    // A fresh view still showing the panel permits another attempt. Give slow
    // input delivery breathing room without turning a retry count into a
    // route failure; Stop and real connection/input failures remain active.
    const auto target_captured=required?s.menus.captured:s.open_menu.captured;
    if (c.now<menu_retry_at_ || target_captured<menu_retry_at_) {
        menu_waiting_=true;
        progress_.message="Waiting for game menu response";
        return true;
    }
    if (!owned_) { if (!tasks_.acquire(scope_)) return true; owned_=true; }
    if (!required && s.menus.has_value(c,Time(1500)))
        for (const auto& menu:s.menus.value) if (menu.selected==true
            && menu.center().x==point.x && menu.center().y==point.y) { closing_menu_=menu.tab; break; }
    menu_retry_delay_=std::min(Time(1000),std::max(Time(250),menu_retry_delay_*2));
    menu_waiting_=true;
    progress_.message=required?"Opening tracking menu":"Closing game menu";
    const auto rest=game::pointer_rest(c,s);
    Action toggle{progress_.message,{Key::primary},Time(50),point}; toggle.changes_menu=true;
    menu_pending_=submit(scope_,TaskClass::interaction,
        {std::move(toggle),{"Move off menu",{},Time(50),rest}},Time(2000));
    return true;
}
}
