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
struct NectarEstimate {
    std::optional<double> percent;
    bool estimated{};
};
// A display estimate, kept separate from raw observations used by automation.
// Missing pixels are not evidence that a previously observed nectar expired.
class NectarTracker {
    struct Reading { double percent{},uncertainty{}; Time at{}; };
    struct State { std::optional<Reading> accepted,pending; Time confirmed{}; };
    std::array<State,5> states_{};
    std::optional<Time> observed_;
public:
    void observe(Time captured,const NectarLevels& levels);
    std::array<NectarEstimate,5> estimate(Time now) const;
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
