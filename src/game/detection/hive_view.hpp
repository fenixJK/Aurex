#pragma once
#include "localization.hpp"

namespace hive::game {
struct HiveViewDetection final : Detection {
    Observation<Rect> region;
};
std::shared_ptr<const Detector> hive_view_detector();
std::shared_ptr<const Detector> spawn_view_detector();
std::shared_ptr<const LandmarkSource> hive_reference_source(std::shared_ptr<const DetectionSource> pixels,
    std::shared_ptr<const LandmarkSource> proximity);
}
