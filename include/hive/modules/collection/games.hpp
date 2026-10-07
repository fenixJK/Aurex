#pragma once
#include "hive/automation/program.hpp"
#include "hive/game/detection/detection.hpp"

namespace hive::features::collection {
std::unique_ptr<automation::Program> memory_match(std::shared_ptr<const game::DetectionSource>,std::string station);
}
