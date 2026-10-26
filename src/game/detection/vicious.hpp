#pragma once
#include "detection.hpp"

namespace hive::game {
// New chat notifications, not the continued presence of an old message.
struct ViciousSignals { std::optional<Time> engaged,defeated; };
struct ViciousDetection final : Detection { Observation<ViciousSignals> events; };
std::shared_ptr<const Detector> vicious_detector();
Observation<ViciousSignals> read_vicious(const DetectionSource&,const ObservationContext&);
}
