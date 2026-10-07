#pragma once
#include "detection.hpp"
#include <array>

namespace hive::game {
enum class PetalColor { any, white, cyan, green, pink, blue, red };
struct Bloom {
    Point center;
    double radius{};
    std::array<double,7> colors{};
};
struct Blooms final : Detection {
    bool visible{};
    std::vector<Bloom> flowers;
};
std::shared_ptr<const Detector> bloom_detector();
Observation<Blooms> read_blooms(const DetectionSource& source,const ObservationContext& context);
}
