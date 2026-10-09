#pragma once
#include "core/runtime/program.hpp"

namespace hive::automation {
std::unique_ptr<Program> prepare_travel(ProgramFactory movement,ProgramFactory hive,double speed);
}
