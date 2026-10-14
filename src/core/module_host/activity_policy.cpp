#include "activity_policy.hpp"
#include <tuple>

namespace hive::automation {
std::optional<std::string> FairActivityPolicy::choose(std::span<const ActivityOption> options) {
    const ActivityOption* selected=nullptr;
    const auto rank=[](const ActivityOption& option) {
        const bool urgent=option.latest_start && *option.latest_start<=option.now;
        return std::tuple(!urgent,urgent?*option.latest_start:Time{},-option.priority,option.due,option.duration,option.id);
    };
    for (const auto& option : options) {
        if (served_.contains(option.id) && !(option.latest_start && *option.latest_start<=option.now)) continue;
        if (!selected || rank(option)<rank(*selected)) selected=&option;
    }
    return selected?std::optional(selected->id):std::nullopt;
}
void FairActivityPolicy::completed(const std::optional<std::string>& activity) {
    if (activity) served_.insert(*activity);
    else served_.clear();
}
}
