#pragma once
#include "detection.hpp"
#include "screen_reference.hpp"

namespace hive::game {
struct SprinklerReferences final : Detection {
    Rect search;
    std::vector<Rect> candidates;
    bool complete{};
    int image_width{},image_height{};
};
SprinklerReferences find_sprinkler_references(const DetectionInput& input);
std::shared_ptr<const Detector> sprinkler_reference_detector();
std::shared_ptr<const ScreenReferenceSource> sprinkler_reference_source(std::shared_ptr<const DetectionSource> source);
}
