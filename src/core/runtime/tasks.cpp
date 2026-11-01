#include "tasks.hpp"
#include "pointer_tasks.hpp"

namespace hive {
Time input_lifetime(const std::vector<Action>& actions) {
    // Each action starts on a runtime tick. Budget scheduling overhead without
    // changing the requested key holds, releases or movement durations.
    Time lifetime{1000};
    for (const auto& action:actions) lifetime+=action.duration+Time(250);
    return lifetime;
}
const ModuleGrant& TaskBroker::check(ModuleScope scope) const {
    if (scope.generation!=generation_ || !owners_.contains(scope.id)) throw std::logic_error("Module scope is no longer active");
    return owners_.at(scope.id);
}
void TaskBroker::reset(Time now) {
    for (const auto& [id,grant] : owners_) { runtime_.cancel_owner(id,now); runtime_.take_results(id); }
    owners_.clear(); control_.reset(); ++generation_;
}
ModuleScope TaskBroker::grant(ModuleGrant grant) {
    if (grant.name.empty() || grant.name.size()>80 || owners_.size()>=128 || grant.task_classes.empty()) throw std::invalid_argument("Invalid module grant");
    for (const auto& [id,existing] : owners_) if (existing.name==grant.name) throw std::invalid_argument("Module already registered");
    for (auto c : grant.task_classes) if (c!=TaskClass::activity && c!=TaskClass::travel && c!=TaskClass::interaction && c!=TaskClass::recovery) throw std::invalid_argument("Unknown task class");
    const auto id=next_owner_++; owners_.emplace(id,std::move(grant)); return {id,generation_};
}
bool TaskBroker::acquire(ModuleScope scope) {
    check(scope);
    const auto state=runtime_.status().state;
    if (state!=RunState::running) return false;
    if (control_) return *control_==scope;
    if (runtime_.has_work()) return false;
    control_=scope; return true;
}
void TaskBroker::release(ModuleScope scope) {
    check(scope);
    if (control_ && *control_==scope) {
        if (runtime_.has_work()) throw std::logic_error("Finish or cancel module work before releasing control");
        control_.reset();
    }
}
std::string TaskBroker::submit(ModuleScope scope,TaskClass kind,std::vector<Action> actions,Time now,Time lifetime) {
    const auto& grant=check(scope);
    if (!control_ || *control_!=scope) throw std::logic_error("Module does not own game control");
    if (!grant.task_classes.contains(kind)) throw std::invalid_argument("Task class was not granted to this module");
    if (runtime_.status().state!=RunState::running) throw std::logic_error("Session is not running");
    if (actions.empty() || actions.size()>512 || lifetime<Time(1) || lifetime>std::chrono::minutes(10)) throw std::invalid_argument("Invalid module task bounds");
    Time duration{};
    for (const auto& a : actions) {
        if (a.duration<Time(1) || a.duration>=lifetime) throw std::invalid_argument("Module action must fit within its task lifetime");
        duration+=a.pacing?Time(1):a.duration;
        if (a.concurrent_pointer && (a.concurrent_pointer->scope()!=scope || !grant.capabilities.contains(Capability::pointer) || !grant.capabilities.contains(Capability::interaction)))
            throw std::invalid_argument("Concurrent pointer interaction was not granted");
        if (a.pointer && !grant.capabilities.contains(Capability::pointer)) throw std::invalid_argument("Pointer access was not granted");
        if (a.scroll && !grant.capabilities.contains(Capability::interaction)) throw std::invalid_argument("Scroll interaction was not granted");
        if (a.tap && !grant.capabilities.contains(*a.tap==Key::jump || camera_key(*a.tap)?Capability::movement:Capability::interaction))
            throw std::invalid_argument("Key tap capability was not granted");
        for (auto key : a.held) {
            auto capability=(key==Key::primary || key==Key::interact || hotbar_key(key) || key>=Key::digit_0)?Capability::interaction:Capability::movement;
            if (!grant.capabilities.contains(capability)) throw std::invalid_argument("Input capability was not granted");
        }
    }
    if (duration>=lifetime) throw std::invalid_argument("Task cannot finish before its deadline");
    // Settling is a requested duration, not a module-level scheduling limit.
    // Yield during long key-free waits without repeating their initial tap,
    // pointer move or scroll. Actual holds retain their authored timing;
    // Stop/focus guards still run every tick, and paced walking bounds its
    // own feedback interval in Runtime.
    std::vector<Action> scheduled;
    scheduled.reserve(actions.size());
    for (auto& action:actions) {
        Time tail{};
        const bool continuous=action.continue_movement;
        if (action.held.empty() && !action.pacing && !action.pointer_to && action.duration>Time(250)) {
            tail=action.duration-Time(250);
            action.duration=Time(250);
            // Empty slices share one timeline instead of accumulating a new
            // tick's rounding delay at every scheduling boundary.
            if (!action.pointer && !action.scroll && !action.tap) action.continue_movement=true;
        }
        const auto label=action.label;
        const auto pointer=action.concurrent_pointer;
        scheduled.push_back(std::move(action));
        while (tail>Time{}) {
            Action wait{label,{},std::min(tail,Time(250)),{}};
            wait.concurrent_pointer=pointer;
            tail-=wait.duration;
            wait.continue_movement=tail>Time{} || continuous;
            scheduled.push_back(std::move(wait));
        }
    }
    auto id=grant.name+"/"+std::to_string(next_task_++);
    runtime_.enqueue({id,static_cast<int>(kind),now,now+lifetime,std::move(scheduled),scope.id});
    return id;
}
std::vector<TaskResult> TaskBroker::results(ModuleScope scope) { check(scope); return runtime_.take_results(scope.id); }
void TaskBroker::cancel(ModuleScope scope,Time now) { check(scope); runtime_.cancel_owner(scope.id,now); }
void TaskBroker::revoke(ModuleScope scope,Time now) {
    check(scope); cancel(scope,now); runtime_.take_results(scope.id);
    if (control_ && *control_==scope) control_.reset(); owners_.erase(scope.id);
}
}
