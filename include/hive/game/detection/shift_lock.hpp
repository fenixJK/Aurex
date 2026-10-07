#pragma once
#include "hive/core/model.hpp"
namespace hive::game {
Observation<bool> read_shift_lock(const Frame& frame,std::uint64_t generation);
}
