#pragma once
#include "detection.hpp"

namespace hive::game {
inline constexpr Time field_boost_duration=std::chrono::minutes(15);
struct FieldBoost { std::string field; std::optional<unsigned> stacks; std::optional<Time> remaining; };
struct FieldBoostDetection final : Detection {
    bool readable{};
    std::vector<FieldBoost> boosts;
};
std::shared_ptr<const Detector> field_boost_detector();
}
