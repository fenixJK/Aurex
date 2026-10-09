#include "progress.hpp"
#include <cmath>

namespace hive::features::combat {
void FightProgress::begin(int maximum,int initial) {
    if (maximum<0 || initial<0 || initial>maximum) throw std::invalid_argument("Invalid boss health");
    std::lock_guard lock(mutex_);
    maximum_=maximum; value_={}; value_.maximum=maximum; baseline_.reset(); last_=Time(-1);
    if (initial) value_.health=initial;
}
void FightProgress::sample(double fraction,Time now) {
    if (!std::isfinite(fraction) || fraction<0 || fraction>1 || now<Time{}) return;
    std::lock_guard lock(mutex_);
    if (!maximum_ || now<=last_) return;
    const auto health=fraction*maximum_;
    if (!baseline_ || now-last_>std::chrono::minutes(2) || health>*baseline_+maximum_*.025) {
        baseline_=health; baseline_time_=now;
        value_.damage_per_second.reset(); value_.remaining.reset();
    }
    last_=now; value_.health=health;
    const auto elapsed=now-baseline_time_;
    const auto damage=*baseline_-health;
    if (elapsed>=Time(10000) && damage>=maximum_*.025) {
        value_.damage_per_second=damage*1000/elapsed.count();
        baseline_=health; baseline_time_=now;
    }
    if (value_.damage_per_second) value_.remaining=Time(std::llround(health/ *value_.damage_per_second*1000));
}
void FightProgress::finish(bool defeated,std::optional<Time> retry) {
    std::lock_guard lock(mutex_);
    value_.defeated=defeated; value_.unavailable=false; value_.retry=retry;
    if (defeated) { value_.health=0; value_.remaining=Time{}; }
}
void FightProgress::finish_unavailable(Time retry) {
    std::lock_guard lock(mutex_);
    value_.defeated=false; value_.unavailable=true; value_.retry=retry;
}
FightSnapshot FightProgress::read() const { std::lock_guard lock(mutex_); return value_; }
}
