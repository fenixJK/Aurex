#pragma once
#include "program.hpp"
#include "hive/game/detection/respawn.hpp"

namespace hive::automation {
std::unique_ptr<Program> request_respawn(std::shared_ptr<const game::RespawnSource> source);
std::unique_ptr<Program> wait_for_respawn(std::shared_ptr<const game::RespawnSource> source);
}
