#pragma once
#include "core/runtime/model.hpp"
#include <mutex>
#include <stdexcept>

namespace hive::game {
struct SprinklerPreference {
    std::string equipment{"None"};
    unsigned slot{1};
};
struct ConversionPreference {
    bool balloon{};
    Time interval{};
    bool after_gather{};
    bool with_backpack{};
};
class HiveConversion {
    mutable std::mutex mutex_;
    ConversionPreference preference_;
    std::optional<Time> completed_;
public:
    void configure(ConversionPreference preference) {
        if(preference.interval<Time{} || preference.interval>std::chrono::hours(12)) throw std::invalid_argument("Invalid balloon interval");
        std::lock_guard lock(mutex_); preference_=preference;
    }
    bool due(const ObservationContext& context,bool gathered=false) const {
        std::lock_guard lock(mutex_);
        return preference_.balloon && (!preference_.after_gather || gathered)
            && (!completed_ || context.now<*completed_ || context.now-*completed_>=preference_.interval);
    }
    bool with_backpack() const { std::lock_guard lock(mutex_); return preference_.with_backpack; }
    void completed(const ObservationContext& context) { std::lock_guard lock(mutex_); completed_=context.now; }
};
}
