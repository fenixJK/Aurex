#pragma once
#include "game/movement/field_plans.hpp"

namespace hive::policies {
class DefaultFieldPolicy final : public game::FieldPolicy {
public:
    std::optional<std::string> choose(std::span<const game::FieldOption> options,
        const std::optional<std::string>& current_field) const override;
};
}
