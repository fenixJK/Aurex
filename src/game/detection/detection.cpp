#include "detection.hpp"

namespace hive::game {
std::optional<DetectionResult> DetectionChannel::read() const {
    if (on_demand_) {
        const auto now=monotonic_now().count();
        const auto previous=requested_until_.exchange(now+2000);
        if (wake_ && previous<=now) { ++wake_->revision; wake_->changed.notify_all(); }
    }
    return peek();
}
std::optional<DetectionResult> DetectionChannel::peek() const { std::lock_guard lock(mutex_); return value_; }
void DetectionChannel::clear() { std::lock_guard lock(mutex_); value_.reset(); requested_until_=0; }
void DetectionChannel::publish(DetectionResult result) {
    std::lock_guard lock(mutex_);
    if (result.value || !result.error.empty()) value_=std::move(result);
}
}
