#pragma once
#include "core/runtime/program.hpp"
#include "game/detection/detection.hpp"

namespace hive::features::collection {
std::unique_ptr<automation::Program> memory_match(std::shared_ptr<const game::DetectionSource>,std::string station);
}
