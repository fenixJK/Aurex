#pragma once
#include "detection.hpp"

namespace hive::game {
// Fresh notifications plus current local nameplate evidence. Old chat is not replayed.
struct ViciousSignals {
    std::optional<Time> engaged,defeated;
    // Current local nameplate, independent of when a server-wide chat message arrived.
    bool visible{};
};
struct ViciousDetection final : Detection { Observation<ViciousSignals> events; };
std::shared_ptr<const Detector> vicious_detector();
Observation<ViciousSignals> read_vicious(const DetectionSource&,const ObservationContext&);
}
