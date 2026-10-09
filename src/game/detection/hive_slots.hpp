#pragma once
#include "detection.hpp"
#include <array>

namespace hive::game {
using HiveSlotCandidates=std::array<Knowledge,6>;
struct HiveSlotsDetection final : Detection {
    Observation<HiveSlotCandidates> candidates;
};
class HiveSlotsSource {
public:
    virtual ~HiveSlotsSource()=default;
    virtual Observation<HiveSlotCandidates> read(const ObservationContext& context) const=0;
};
std::shared_ptr<const Detector> hive_slots_detector();
std::shared_ptr<const HiveSlotsSource> hive_slots_source(std::shared_ptr<const DetectionSource> source);
}
