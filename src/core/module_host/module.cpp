#include "module.hpp"

namespace hive::extensions {
void validate(const RoutineParticipation& p) {
    if (p.priority<0 || p.priority>100 || (p.demand_only?p.recheck!=Time{}:(p.recheck<Time(1000) || p.recheck>std::chrono::hours(24))))
        throw std::invalid_argument("Invalid routine participation");
}
std::string run_state_name(RunState state) {
    switch (state) {
    case RunState::idle: return "Ready";
    case RunState::running: return "Running";
    case RunState::paused: return "Paused";
    case RunState::completed: return "Completed";
    case RunState::stopped: return "Stopped";
    case RunState::faulted: return "Needs attention";
    }
    return "Unknown";
}
}
