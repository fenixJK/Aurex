#pragma once
#include "core/runtime/model.hpp"
#include <mutex>
#include <optional>

namespace hive::coordination {
struct MainBoost {
    std::string field;
    Time expires{},updated{};
    std::uint64_t generation{};
    bool supporting{};
};
// One local handoff for following, the overview and hotbar decisions. Network
// expiry is handled by State; this lease also expires if the module stops ticking.
class MainBoostState {
    mutable std::mutex mutex_;
    std::optional<MainBoost> value_;
public:
    void update(std::optional<MainBoost> value) { std::lock_guard lock(mutex_); value_=std::move(value); }
    std::optional<MainBoost> read(Time now,std::uint64_t generation=0) const {
        std::lock_guard lock(mutex_);
        if (!value_ || now<value_->updated || now-value_->updated>Time(3000) || now>=value_->expires
            || (generation && value_->generation!=generation)) return {};
        return value_;
    }
};
}
