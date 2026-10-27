#pragma once
#include "core/runtime/program.hpp"
#include "game/detection/respawn.hpp"

namespace hive::automation {
// A caller accepting an unconfirmed transition must verify the player's
// location before issuing movement. Closing a menu alone does not prove reset.
struct RespawnResult { Observation<bool> transition; };
std::unique_ptr<Program> request_respawn(std::shared_ptr<const game::RespawnSource> source,
    std::shared_ptr<RespawnResult> result={});
std::unique_ptr<Program> wait_for_respawn(std::shared_ptr<const game::RespawnSource> source);
}
