#pragma once
#include "state.hpp"

namespace hive::game {
struct FeedingDialog { Point feed,cancel,input; unsigned amount{}; };
Observation<FeedingDialog> read_feeding_dialog(const ObservationContext& context,const State& state);
}
