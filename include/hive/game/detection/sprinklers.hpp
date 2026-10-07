#pragma once
#include "state.hpp"
#include "detection.hpp"
#include <memory>

namespace hive::game {
enum class SprinklerFeedback { none, too_low, too_high, unavailable };
struct SprinklerEquipment { std::string name; unsigned count; bool jump; };
std::span<const SprinklerEquipment> sprinkler_equipment();
Observation<SprinklerFeedback> read_sprinkler_feedback(const ObservationContext& context,const State& state,Time minimum={});
struct SprinklerSite {
    std::string field,site;
    unsigned builder_slot{};
    bool aligned{},ready{},placed{};
    SprinklerFeedback feedback{SprinklerFeedback::none};
};
struct SprinklerDetection final : Detection { SprinklerSite site; };
class SprinklerSource {
public:
    virtual ~SprinklerSource()=default;
    virtual Observation<SprinklerSite> read(const ObservationContext& context,const State& state) const=0;
};
std::shared_ptr<const SprinklerSource> sprinkler_source(std::shared_ptr<const DetectionSource> source);
}
