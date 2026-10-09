#pragma once
#include "planner.hpp"
#include <functional>

namespace hive::features::planters {
struct Growing {
    Stats stats;
    Phase phase{};
    double age{}, due_in{};
    Travel travel;
};
struct PlanningSnapshot {
    std::vector<Group> groups;
    std::vector<Growing> growing;
    std::size_t capacity{3};
    double now{};
};
struct Ledger;
PlanningSnapshot planning_snapshot(const Ledger& ledger,std::vector<Group> groups,const TravelTimes& travel,
    std::chrono::sys_time<Time> wall,double session_seconds,std::size_t capacity=3);
std::vector<Group> planning_groups(const PlanningSnapshot& snapshot);
enum class PlanReason { no_capacity, unavailable, covered, stable, shortage, partial_readings };
struct PlanDecision {
    std::vector<Assignment> assignments;
    Selection selection;
    PlanReason reason{};
    std::size_t previews{};
};
struct PlanningLimits {
    std::size_t preview_steps{4096};
    std::function<bool()> cancelled;
};
class AdaptivePlanner {
public:
    PlanDecision select(const PlanningSnapshot& snapshot,PlanningLimits limits={});
    void reset() noexcept;
    std::array<double,5> deficit_seconds() const;
private:
    struct Gap { double age{},at{},value{}; Target target; };
    std::array<std::optional<Gap>,5> gaps_;
    std::optional<double> now_;
    Selection previous_;
};
}
