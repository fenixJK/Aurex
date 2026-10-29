#pragma once
#include "game/detection/daylight.hpp"

namespace hive::features::combat {
// Only fresh, agreeing daylight observations end a search. An obstructed or
// unavailable view is not daylight, and a previous server's frame is unusable.
class SearchDaylight {
    Observation<bool> previous_;
    bool ended_{};
public:
    void observe(const std::shared_ptr<const game::DetectionSource>& source,const ObservationContext& context,Time since) {
        if (!source || ended_) return;
        const auto reading=game::read_night(*source,context,since);
        if (!reading.has_value(context,Time(2000),since) || reading.captured<=previous_.captured) return;
        ended_=!reading.value && previous_.has_value(context,Time(2000),since) && !previous_.value;
        previous_=reading;
    }
    bool ended() const { return ended_; }
};
}
