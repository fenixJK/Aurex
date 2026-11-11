#pragma once
#include "detection.hpp"

namespace hive::game {
inline constexpr Time guiding_star_duration=std::chrono::minutes(10);
struct GuidingActivation { std::string field; Time seen; std::uint64_t sequence{}; };
struct GuidingDetection final : Detection { std::optional<GuidingActivation> activation; };
// The owner's activation notification, not the field aura shared by other players.
std::shared_ptr<const Detector> guiding_detector();
}
