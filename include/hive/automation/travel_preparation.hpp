#pragma once
#include "program.hpp"

namespace hive::automation {
std::unique_ptr<Program> prepare_travel(ProgramFactory movement,ProgramFactory hive,double speed);
}
