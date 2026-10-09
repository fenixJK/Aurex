#include "policies.hpp"
#include <algorithm>
#include <tuple>

namespace hive::policies {
std::optional<std::string> DefaultFieldPolicy::choose(std::span<const game::FieldOption> options,
    const std::optional<std::string>& current_field) const {
    auto rank=[&](const game::FieldOption& option) {
        int priority=-1;
        for (const auto& goal : option.goals) priority=std::max(priority,goal.priority);
        const auto matching=std::count_if(option.goals.begin(),option.goals.end(),[&](const auto& g) { return g.priority==priority; });
        const bool goal=priority>=0;
        return std::tuple(-priority,-matching,goal && option.field!=current_field,
            goal?option.journey.estimate:Time::zero(),option.saved_order);
    };
    const game::FieldOption* selected=nullptr;
    for (const auto& option : options)
        if (!selected || rank(option)<rank(*selected)) selected=&option;
    return selected?std::optional(selected->field):std::nullopt;
}
}
