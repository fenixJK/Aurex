#pragma once
#include "hive/workflows/navigation.hpp"
#include <algorithm>

namespace hive::automation::detail {
inline std::vector<std::string> locations(const std::vector<workflows::ExecutableRoute>& routes) {
    std::set<std::string> result;
    for (const auto& route : routes) {
        result.insert(route.route.from); result.insert(route.route.to);
        for (const auto& checkpoint:route.checkpoints) if (!checkpoint.observation_scope.empty()) result.insert(checkpoint.observation_scope);
    }
    return {result.begin(),result.end()};
}
inline Time estimate(const std::vector<workflows::CheckpointPlan>& plans) {
    Time result{};
    for (const auto& plan : plans) {
        const auto before=result;
        for (const auto& leg : plan.path) result+=Time(static_cast<Time::rep>(std::ceil(leg.tiles*4000/plan.speed)));
        for (const auto& action : plan.approach) result+=action.duration+(plan.continuous?Time{}:plan.camera_settle);
        for (const auto& step : plan.sequence) {
            if (const auto* leg=std::get_if<game::WalkLeg>(&step)) result+=Time(static_cast<Time::rep>(std::ceil(leg->tiles*4000/plan.speed)));
            else result+=std::get<Action>(step).duration;
        }
        if (plan.completion==workflows::CheckpointCompletion::launch) result+=Time(100);
        result=std::max(result,before+plan.minimum_duration);
    }
    return result;
}
}
