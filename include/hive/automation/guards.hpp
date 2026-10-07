#pragma once
#include "program.hpp"

namespace hive::automation {
std::unique_ptr<RunGuard> observation_guard(const RunConfig& config);
std::optional<double> backpack_reading(const ObservationContext& context,const game::State& observations,Time minimum);
}
