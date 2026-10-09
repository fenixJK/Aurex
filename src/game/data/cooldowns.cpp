#include "cooldowns.hpp"
#include <algorithm>

namespace hive::game {
namespace {
constexpr auto maximum=std::chrono::days(7);
WallTime now(const WallClock& clock) {
    const auto value=clock();
    if (value.time_since_epoch()<Time{} || value.time_since_epoch()>Time::max()-maximum)
        throw std::runtime_error("Invalid clock for saved cooldowns");
    return value;
}
}
void validate(const CooldownRecords& records) {
    if (records.size()>128) throw std::invalid_argument("Too many saved cooldowns");
    for (const auto& [id,value] : records) {
        if (id.empty() || id.size()>80 || std::any_of(id.begin(),id.end(),[](unsigned char c) { return c<32 || c>=127; })
            || value.started.time_since_epoch()<Time{} || value.started.time_since_epoch()>Time::max()-maximum
            || value.ready<value.started || value.ready-value.started>maximum)
            throw std::invalid_argument("Invalid saved cooldown");
    }
}
Cooldowns::Cooldowns(std::shared_ptr<CooldownStore> store,WallClock clock):store_(std::move(store)),clock_(std::move(clock)) {
    if (!store_ || !clock_) throw std::invalid_argument("Cooldowns need saved state and a clock");
}
void Cooldowns::load() {
    std::lock_guard lock(mutex_);
    if (loaded_) return;
    auto records=store_->load(); validate(records);
    records_=std::move(records); loaded_=true;
}
void Cooldowns::flush() {
    std::lock_guard lock(mutex_);
    if (dirty_) { store_->save(records_); dirty_=false; }
}
CooldownRecords Cooldowns::records() const { std::lock_guard lock(mutex_); return records_; }
Time Cooldowns::remaining(const std::string& id,Time cooldown) const {
    if (cooldown<Time{} || cooldown>maximum) throw std::invalid_argument("Invalid cooldown duration");
    std::lock_guard lock(mutex_);
    if (!loaded_) throw std::logic_error("Cooldowns have not been loaded");
    const auto time=now(clock_); const auto found=records_.find(id);
    if (found==records_.end() || found->second.started>time) return {};
    const auto ready=found->second.estimated?found->second.started+cooldown:found->second.ready;
    return std::max(Time{},ready-time);
}
void Cooldowns::remember(const std::string& id,Time started,Time ready,bool estimated,const ObservationContext& context) {
    if (!context.generation || !context.window.id || !context.window.bounds.valid()
        || started<Time{} || started>context.now || ready<started || ready-started>maximum)
        throw std::invalid_argument("Invalid cooldown outcome time");
    std::lock_guard lock(mutex_);
    if (!loaded_) throw std::logic_error("Cooldowns have not been loaded");
    const auto time=now(clock_);
    if (time.time_since_epoch()<context.now-started) throw std::runtime_error("Clock precedes cooldown observation");
    const auto began=time-(context.now-started);
    Cooldown value{began,began+(ready-started),estimated};
    validate({{id,value}});
    const auto old=records_.find(id);
    if (old==records_.end() || old->second.started<=began || old->second.started>time) {
        if (old==records_.end() || old->second!=value) {
            auto next=records_; next[id]=value; validate(next); records_=std::move(next); dirty_=true;
        }
    }
    if (dirty_) { validate(records_); store_->save(records_); dirty_=false; }
}
}
