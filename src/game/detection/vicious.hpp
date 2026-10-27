#pragma once
#include "detection.hpp"

namespace hive::game {
// Colour evidence guides a search; it never proves engagement or a kill.
struct ViciousCandidate { Point center; double radius{}; unsigned pixels{}; };
std::vector<ViciousCandidate> vicious_candidates(const Frame&);
// Fresh notifications plus current local nameplate evidence. Old chat is not replayed.
struct ViciousSignals {
    std::optional<Time> engaged,defeated;
    // Current local nameplate, independent of when a server-wide chat message arrived.
    bool visible{};
    std::vector<ViciousCandidate> candidates;
    bool world_visible{};
};
struct ViciousDetection final : Detection { Observation<ViciousSignals> events; };
std::shared_ptr<const Detector> vicious_detector();
Observation<ViciousSignals> read_vicious(const DetectionSource&,const ObservationContext&);
}
