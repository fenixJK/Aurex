#pragma once
#include "detection.hpp"
#include "state.hpp"

namespace hive::game {
struct StateSensor { ObservationKind kind; DetectorConfig config; };
std::vector<StateSensor> state_sensors();
void apply_state_observation(State& state,ObservationKind kind,const Detection& result);
}
