#pragma once
#include "core/runtime/program.hpp"
#include "game/detection/respawn.hpp"

namespace hive::automation {
struct ReconnectResult { Observation<bool> joined; };
std::unique_ptr<Program> request_reconnect(std::shared_ptr<const game::RespawnSource> game_view,std::shared_ptr<ReconnectResult> result={});
std::unique_ptr<Program> wait_for_reconnect(std::shared_ptr<const game::RespawnSource> game_view);
}
