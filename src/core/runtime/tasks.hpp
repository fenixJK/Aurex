#pragma once
#include "runtime.hpp"
#include <map>

namespace hive {
Time input_lifetime(const std::vector<Action>& actions);
struct ModuleScope {
    std::uint64_t id{}, generation{};
    bool operator==(const ModuleScope&) const = default;
};
enum class TaskClass { activity=10, travel=20, interaction=40, recovery=100 };
enum class Capability { movement, interaction, pointer };
struct ModuleGrant {
    std::string name;
    std::set<Capability> capabilities;
    std::set<TaskClass> task_classes;
};
class TaskPort {
public:
    virtual ~TaskPort() = default;
    virtual bool acquire(ModuleScope scope) = 0;
    virtual void release(ModuleScope scope) = 0;
    virtual std::string submit(ModuleScope scope,TaskClass kind,std::vector<Action> actions,Time lifetime) = 0;
    virtual std::vector<TaskResult> results(ModuleScope scope) = 0;
    virtual void cancel(ModuleScope scope) = 0;
};
class TaskBroker {
public:
    explicit TaskBroker(Runtime& runtime): runtime_(runtime) {}
    void reset(Time now);
    ModuleScope grant(ModuleGrant grant);
    bool acquire(ModuleScope scope);
    void release(ModuleScope scope);
    std::string submit(ModuleScope scope,TaskClass kind,std::vector<Action> actions,Time now,Time lifetime);
    std::vector<TaskResult> results(ModuleScope scope);
    void cancel(ModuleScope scope,Time now);
    void revoke(ModuleScope scope,Time now);
    std::uint64_t generation() const { return generation_; }
private:
    Runtime& runtime_;
    std::uint64_t generation_{1},next_owner_{1},next_task_{1};
    std::map<std::uint64_t,ModuleGrant> owners_;
    std::optional<ModuleScope> control_;
    const ModuleGrant& check(ModuleScope scope) const;
};
}
