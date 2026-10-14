#include "policies.hpp"
#include <algorithm>
#include <tuple>
#include <array>

namespace hive::policies {
std::optional<std::string> DefaultFieldPolicy::choose(std::span<const game::FieldOption> options,
    const std::optional<std::string>& current_field) const {
    auto rank=[&](const game::FieldOption& option) {
        std::array<int,101> coverage{};
        for (const auto& goal:option.goals) --coverage.at(100-goal.priority);
        const bool goal=!option.goals.empty();
        return std::tuple(coverage,goal && option.field!=current_field,
            goal?option.journey.estimate:Time::zero(),option.saved_order);
    };
    const game::FieldOption* selected=nullptr;
    for (const auto& option : options)
        if (!selected || rank(option)<rank(*selected)) selected=&option;
    return selected?std::optional(selected->field):std::nullopt;
}
}
