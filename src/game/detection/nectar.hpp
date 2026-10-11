#pragma once
#include "detection.hpp"
#include "state.hpp"
#include <array>

namespace hive::game {
enum class Nectar { comforting,refreshing,satisfying,motivating,invigorating };
struct NectarLevels {
    std::array<std::optional<double>,5> percent{};
    double uncertainty_percent{};
    Rect bar;
    std::size_t known() const;
};
Observation<NectarLevels> read_nectar(const DetectionInput& input);
struct NectarDetection final : Detection { Observation<NectarLevels> levels; };
class NectarSource {
public:
    virtual ~NectarSource()=default;
    virtual Observation<NectarLevels> read(const ObservationContext&) const=0;
};
std::shared_ptr<const Detector> nectar_detector();
std::shared_ptr<const NectarSource> nectar_source(std::shared_ptr<const DetectionSource> channel);
}
