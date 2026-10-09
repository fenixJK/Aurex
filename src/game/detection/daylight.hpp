#pragma once
#include "detection.hpp"

namespace hive::game {
struct Daylight final : Detection { std::optional<bool> night; };
std::shared_ptr<const Detector> daylight_detector();
std::function<bool(const ObservationContext&)> during_night(std::shared_ptr<const DetectionSource> source);
}
