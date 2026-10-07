#pragma once
#include "detection.hpp"
#include <span>
#include <string_view>

namespace hive::game {
struct BuffType { std::string_view name; unsigned maximum; };
struct BuffState { std::string name; std::optional<bool> active; std::optional<unsigned> stacks; };
struct BuffDetection final : Detection { std::vector<BuffState> buffs; };
std::span<const BuffType> tracked_buffs();
std::shared_ptr<const Detector> buff_detector();
}
