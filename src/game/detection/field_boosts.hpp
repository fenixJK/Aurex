#pragma once
#include "detection.hpp"

namespace hive::game {
struct FieldBoost { std::string field; };
struct FieldBoostDetection final : Detection {
    bool readable{};
    std::vector<FieldBoost> boosts;
};
std::shared_ptr<const Detector> field_boost_detector();
}
