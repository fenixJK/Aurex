#pragma once
#include "hive/core/model.hpp"
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
};
class HiveConversion {
    mutable std::mutex mutex_;
    ConversionPreference preference_;
    std::optional<ObservationContext> completed_;
public:
    void configure(ConversionPreference preference) {
        if(preference.interval<Time{} || preference.interval>std::chrono::hours(12)) throw std::invalid_argument("Invalid balloon interval");
        std::lock_guard lock(mutex_); preference_=preference;
    }
    bool due(const ObservationContext& context) const {
        std::lock_guard lock(mutex_);
        return preference_.balloon && (!completed_ || completed_->generation!=context.generation
            || completed_->window.id!=context.window.id || completed_->window.process!=context.window.process
            || context.now<completed_->now || context.now-completed_->now>=preference_.interval);
    }
    void completed(const ObservationContext& context) { std::lock_guard lock(mutex_); completed_=context; }
};
}
