#pragma once
#include "hive/automation/activity_policy.hpp"
#include "hive/game/movement/field_plans.hpp"

namespace hive::policies {
class DefaultFieldPolicy final : public game::FieldPolicy {
public:
    std::optional<std::string> choose(std::span<const game::FieldOption> options,
        const std::optional<std::string>& current_field) const override;
};
class FairActivityPolicy final : public automation::ActivityPolicy {
    std::set<std::string> served_;
public:
    std::optional<std::string> choose(std::span<const automation::ActivityOption> options) override;
    void completed(const std::optional<std::string>& activity) override;
};
}
