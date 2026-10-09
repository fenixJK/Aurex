#include "activity_policy.hpp"
#include <tuple>

namespace hive::automation {
std::optional<std::string> FairActivityPolicy::choose(std::span<const ActivityOption> options) {
    const ActivityOption* selected=nullptr;
    for (const auto& option : options) {
        if (served_.contains(option.id)) continue;
        if (!selected || std::tuple(-option.priority,option.due,option.id)<std::tuple(-selected->priority,selected->due,selected->id)) selected=&option;
    }
    return selected?std::optional(selected->id):std::nullopt;
}
void FairActivityPolicy::completed(const std::optional<std::string>& activity) {
    if (activity) served_.insert(*activity);
    else served_.clear();
}
}
