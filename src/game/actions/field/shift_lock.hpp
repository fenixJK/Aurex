#pragma once
#include "core/runtime/program.hpp"
namespace hive::automation {
std::unique_ptr<Program> set_shift_lock(bool enabled);
}
