#pragma once
#include "tasks.hpp"

namespace hive {
class PointerTasks final : public TaskPort {
    ModuleScope scope_;
    std::vector<Action> actions_;
    std::vector<TaskResult> results_;
    std::string id_;
    Time now_{},expires_{},until_{};
    std::size_t index_{},next_id_{};
    bool started_{},begun_{},pressed_{};
    void check(ModuleScope scope) const;
    void finish(TaskOutcome outcome);
    void release(Input& input);
public:
    explicit PointerTasks(ModuleScope scope):scope_(scope) {}
    ModuleScope scope() const { return scope_; }
    void clock(Time now) { now_=now; }
    bool engaged() const { return started_ || pressed_; }
    void tick(Input& input,const Window& window,Time now,bool can_start);
    void stop(Input& input,Time now);
    bool acquire(ModuleScope scope) override;
    void release(ModuleScope scope) override;
    std::string submit(ModuleScope scope,TaskClass kind,std::vector<Action> actions,Time lifetime) override;
    std::vector<TaskResult> results(ModuleScope scope) override;
    void cancel(ModuleScope scope) override;
};
}
