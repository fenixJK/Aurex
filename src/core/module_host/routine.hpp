#pragma once
#include "host.hpp"
#include "activity_policy.hpp"

namespace hive::extensions {
class Routine {
    std::vector<RoutineEntry> entries_;
    std::vector<Time> due_,deferred_until_;
    std::vector<std::uint64_t> after_work_;
    std::vector<bool> pending_at_dispatch_;
    std::uint64_t completed_work_{};
    std::unique_ptr<automation::ActivityPolicy> policy_;
    std::optional<std::size_t> active_;
    Time last_{};
    std::uint64_t generation_{};
    std::uint64_t session_id_{};
    struct Readiness { bool pending{},eligible{}; };
    Readiness readiness(Host& host,const SessionView& view,const WorkSnapshot& schedule,std::size_t index,bool idle_retry=false) const;
    RoutineParticipation participation(const WorkSnapshot& schedule,std::size_t index) const;
    void check(Time now);
public:
    Routine(std::vector<RoutineEntry> entries,std::unique_ptr<automation::ActivityPolicy> policy);
    void launch_failed() { active_.reset(); }
    bool should_yield(Host& host,const SessionView& view) const;
    std::optional<automation::Launch> next(Host& host,const SessionView& view,bool allow_launch=true);
};
}
