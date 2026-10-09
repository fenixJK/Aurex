#pragma once
#include "core/runtime/program.hpp"
#include "game/detection/respawn.hpp"

namespace hive::automation {
std::unique_ptr<Program> request_reconnect(std::shared_ptr<const game::RespawnSource> game_view);
std::unique_ptr<Program> wait_for_reconnect(std::shared_ptr<const game::RespawnSource> game_view);
}
