#pragma once
#include "core/runtime/model.hpp"

namespace hive {
class RejoinSchedule {
    Time interval_{},offset_{},next_{};
public:
    void configure(int hours,int minute,Time now) {
        if (hours<0 || hours>24 || minute<0 || minute>59) throw std::invalid_argument("Invalid reconnect schedule");
        interval_=std::chrono::hours(hours); offset_=std::chrono::minutes(minute); arm(now);
    }
    void arm(Time now) {
        if (interval_==Time{}) { next_={}; return; }
        const auto shifted=now-offset_;
        auto cycles=shifted.count()/interval_.count();
        if (shifted.count()<0 && shifted.count()%interval_.count()) --cycles;
        next_=offset_+interval_*(cycles+1);
    }
    bool due(Time now) const { return interval_>Time{} && now>=next_; }
    Time next() const { return next_; }
};
}
