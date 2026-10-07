#pragma once
#include "program.hpp"
#include "hive/game/detection/screen_reference.hpp"

namespace hive::automation {
std::unique_ptr<Program> recenter(std::shared_ptr<const game::ScreenReferenceSource> source,double speed);
}
