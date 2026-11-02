#pragma once
#include "state.hpp"

namespace hive::game {
struct FeedingDialog { Point feed,cancel,input; unsigned amount{}; };
std::optional<Rect> feeding_quantity_region(const ObservationContext& context,const State& state);
// Without require_amount, locate controls only; amount is not an observed value.
Observation<FeedingDialog> read_feeding_dialog(const ObservationContext& context,const State& state,bool require_amount=true);
}
