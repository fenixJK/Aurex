#pragma once
#include "detection.hpp"
#include "game/movement/movement.hpp"

namespace hive::game {
struct MovementBuffCandidate {
    std::string name;
    Rect pixels;
    std::optional<unsigned> stacks;
};
struct MovementBuffCandidates {
    Rect bar;
    bool layout_known{};
    std::vector<MovementBuffCandidate> candidates;
};
MovementBuffCandidates find_movement_buffs(const DetectionInput& input);
struct MovementBuffDetection final : Detection { MovementBuffCandidates buffs; };
std::shared_ptr<const Detector> movement_buff_detector();
std::shared_ptr<const MovementSpeedSource> movement_speed_source(std::shared_ptr<const DetectionSource> source,double base_speed);
}
