#pragma once
#include "core/runtime/program.hpp"

namespace hive::automation {
// Optional cleanup also used on startup/rejoin. Only a confirmed Ant result
// authorizes a click; absence completes without changing equipment.
std::unique_ptr<Program> keep_ant_amulet();
}
