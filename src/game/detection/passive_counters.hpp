#pragma once
#include "detection.hpp"

namespace hive::game {
struct PassiveCounter { std::string name; std::optional<unsigned> charges; };
struct PassiveCounters final : Detection { std::vector<PassiveCounter> counters; };
// Equipment progress above the hotbar, separate from the temporary buff bar.
std::shared_ptr<const Detector> passive_counter_detector();
}
