#pragma once
#include "detection.hpp"
#include <span>
#include <string_view>

namespace hive::game {
struct BuffType { std::string_view name; unsigned maximum; std::string_view summary_id{}; };
struct BuffState { std::string name; std::optional<bool> active; std::optional<unsigned> stacks; std::optional<double> value; };
struct BuffDetection final : Detection { std::vector<BuffState> buffs; };
struct BuffSnapshot { std::shared_ptr<const BuffDetection> value; Time captured{}; };
std::optional<BuffSnapshot> buff_snapshot(const std::shared_ptr<const DetectionSource>& source,const ObservationContext& context);
std::span<const BuffType> tracked_buffs();
std::shared_ptr<const Detector> buff_detector();
}
