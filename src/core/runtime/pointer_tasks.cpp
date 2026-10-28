#include "pointer_tasks.hpp"
#include "input_clock.hpp"
#include <utility>

namespace hive {
void PointerTasks::check(ModuleScope scope) const {
    if (!scope.id || scope!=scope_) throw std::logic_error("Pointer task belongs to another session");
}
bool PointerTasks::acquire(ModuleScope scope) { check(scope); return true; }
void PointerTasks::release(ModuleScope scope) { check(scope); }
std::string PointerTasks::submit(ModuleScope scope,TaskClass kind,std::vector<Action> actions,Time lifetime) {
    check(scope);
    if (kind!=TaskClass::interaction || !id_.empty() || actions.empty() || actions.size()>8 || results_.size()>8
        || lifetime<Time(1) || lifetime>Time(10000)) throw std::invalid_argument("Invalid concurrent pointer task");
    Time duration{};
    bool positioned=false;
    for (const auto& action:actions) {
        // Subsequent actions may use the position established in this batch,
        // as in the main executor. Never inherit an unrelated cursor position.
        positioned=positioned || action.pointer.has_value();
        if (!positioned || action.pointer_to || action.pacing || action.continue_movement || action.tap
            || (action.held!=std::vector<Key>{} && action.held!=std::vector{Key::primary})
            || action.duration<Time(1) || action.duration>Time(250) || action.scroll<-8 || action.scroll>8
            || (action.scroll && !action.held.empty()) || (action.pointer && (!std::isfinite(action.pointer->x) || !std::isfinite(action.pointer->y)
            || action.pointer->x<0 || action.pointer->x>=1 || action.pointer->y<0 || action.pointer->y>=1)))
            throw std::invalid_argument("Concurrent pointer work only supports bounded clicks and scrolling");
        duration+=action.duration;
    }
    if (duration>=lifetime || duration>Time(500)) throw std::invalid_argument("Concurrent pointer task is too long");
    changes_menu_=std::any_of(actions.begin(),actions.end(),[](const auto& action) { return action.changes_menu; });
    id_="pointer/"+std::to_string(++next_id_); actions_=std::move(actions); index_=0;
    started_=begun_=false; expires_=lifetime;
    return id_;
}
std::vector<TaskResult> PointerTasks::results(ModuleScope scope) { check(scope); return std::exchange(results_,{}); }
void PointerTasks::finish(TaskOutcome outcome) {
    if (started_ && changes_menu_) menu_changed_=std::max(menu_changed_,now_);
    if (!id_.empty()) results_.push_back({id_,scope_.id,outcome,now_,index_,started_});
    id_.clear(); actions_.clear(); index_=0; started_=begun_=false;
}
void PointerTasks::cancel(ModuleScope scope) { check(scope); finish(TaskOutcome::cancelled); }
void PointerTasks::release(Input& input) {
    if (pressed_) { input.key(Key::primary,false); pressed_=false; }
}
void PointerTasks::stop(Input& input,Time now) { now_=now; release(input); finish(TaskOutcome::cancelled); }
void PointerTasks::tick(Input& input,const Window& window,Time now,bool can_start) {
    const InputClock input_clock(now);
    now_=now;
    if (id_.empty()) { release(input); return; }
    if (!started_) {
        release(input);
        if (!can_start) return;
        expires_+=now;
    }
    if (now>=expires_) { release(input); finish(TaskOutcome::expired); return; }
    if (begun_) {
        if (now<until_) return;
        release(input); now_=now=input_clock.now(); begun_=false;
        if (++index_==actions_.size()) { finish(TaskOutcome::executed); return; }
    }
    const auto& action=actions_.at(index_);
    if (action.pointer)
        input.pointer({window.bounds.x+action.pointer->x*window.bounds.width,window.bounds.y+action.pointer->y*window.bounds.height});
    if (!action.held.empty()) { pressed_=true; input.key(Key::primary,true); }
    if (action.scroll) input.scroll(action.scroll);
    now_=now=input_clock.now();
    started_=begun_=true; until_=now+action.duration;
}
}
