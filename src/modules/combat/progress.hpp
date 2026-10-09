#pragma once
#include "core/runtime/model.hpp"
#include <mutex>

namespace hive::features::combat {
struct FightSnapshot {
    std::optional<double> health;
    std::optional<double> damage_per_second;
    std::optional<Time> remaining;
    std::optional<Time> retry;
    bool defeated{},unavailable{};
    int maximum{};
};
class FightProgress {
    mutable std::mutex mutex_;
    FightSnapshot value_;
    std::optional<double> baseline_;
    Time baseline_time_{},last_{Time(-1)};
    int maximum_{};
public:
    void begin(int maximum,int initial=0);
    void sample(double fraction,Time now);
    void finish(bool defeated,std::optional<Time> retry={});
    void finish_unavailable(Time retry);
    FightSnapshot read() const;
};
}
