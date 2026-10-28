#pragma once
#include "model.hpp"

namespace hive {
// Keep supplied (including simulated) time, while accounting for time spent
// delivering native input before starting a hold or the next action.
class InputClock {
    Time base_;
    std::chrono::steady_clock::time_point started_{std::chrono::steady_clock::now()};
public:
    explicit InputClock(Time now):base_(now) {}
    Time now() const { return base_+std::chrono::duration_cast<Time>(std::chrono::steady_clock::now()-started_); }
};
}
