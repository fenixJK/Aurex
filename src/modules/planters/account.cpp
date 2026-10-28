#include "account.hpp"
#include <algorithm>

namespace hive::features::planters {
Account::Account(std::shared_ptr<Store> store):store_(std::move(store)) {
    if (!store_) throw std::invalid_argument("Planters needs an account store");
    publish(saved());
}
std::vector<Plant> Account::plants() const { std::lock_guard lock(snapshot_mutex_); return plants_; }
MaterialHistory Account::materials() const { std::lock_guard lock(snapshot_mutex_); return materials_; }
std::size_t Account::placement_capacity(std::size_t configured) const {
    std::lock_guard lock(snapshot_mutex_); return planters::placement_capacity(placement_limits_,configured);
}
std::size_t Account::configure_capacity(std::size_t configured) {
    if (configured>3) throw std::invalid_argument("Invalid planter capacity");
    bool increased;
    {
        std::lock_guard lock(snapshot_mutex_);
        increased=configured_capacity_ && configured>*configured_capacity_;
    }
    // Raising Slots explicitly asks to retry the previously rejected capacity.
    if (increased) retry_placements();
    std::lock_guard lock(snapshot_mutex_);
    configured_capacity_=configured;
    return planters::placement_capacity(placement_limits_,configured);
}
void Account::defer_placements(WallTime now) {
    std::lock_guard lock(snapshot_mutex_); placement_retry_=now+std::chrono::minutes(5);
}
std::optional<WallTime> Account::placement_retry() const {
    std::lock_guard lock(snapshot_mutex_); return placement_retry_;
}
bool Account::resume_placements(WallTime now) {
    std::lock_guard lock(snapshot_mutex_);
    if (!placement_retry_ || now<*placement_retry_) return false;
    placement_retry_.reset(); return true;
}
void Account::retry_placements() {
    if (busy_.exchange(true)) throw std::runtime_error("Planters is using these timers. Try again after its current run.");
    try {
        auto ledger=saved(); ledger.placement_limits={}; validate(ledger); store_->save(ledger);
        publish(ledger); ++edit_revision_;
        std::lock_guard lock(snapshot_mutex_); placement_retry_.reset();
    } catch (...) { busy_=false; throw; }
    busy_=false;
}
Ledger Account::saved() const { return store_->load().value_or(Ledger{}); }
void Account::apply_offline(Controller& controller) {
    std::lock_guard lock(snapshot_mutex_);
    if (offline_.empty()) return;
    while (!offline_.empty()) {
        controller.exclude_offline(offline_.front().first,offline_.front().second);
        offline_.erase(offline_.begin());
    }
    plants_=controller.ledger().plants; materials_=controller.ledger().materials;
}
void Account::connection(bool reconnecting,WallTime now) {
    {
        std::lock_guard lock(snapshot_mutex_);
        if (reconnecting) { if (!offline_since_) offline_since_=now; return; }
        if (!offline_since_) return;
        if (now>*offline_since_) offline_.emplace_back(*offline_since_,now);
        offline_since_.reset();
    }
    if (busy_.exchange(true)) return;
    try { Controller controller(store_); apply_offline(controller); }
    catch (...) { busy_=false; throw; }
    busy_=false;
}
PlanningSnapshot Account::forecast_state(std::vector<Group> groups,const Preferences& preferences,WallTime now,std::size_t capacity) const {
    const auto ledger=saved();
    if (ledger.pending) throw std::runtime_error("A planter action is in progress. Try again after it finishes.");
    TravelTimes travel;
    for (const auto& [field,samples]:ledger.travel) for (double seconds:samples) travel.record(field,seconds);
    {
        std::lock_guard lock(snapshot_mutex_);
        for (auto& group:groups) {
            const auto i=static_cast<std::size_t>(group.nectar);
            if (nectar_phases_.at(i)) group.phase=*nectar_phases_[i];
            for (auto& candidate:group.candidates) candidate.travel=travel.estimate(candidate.stats.field);
        }
    }
    apply_preferences(groups,preferences);
    auto snapshot=planning_snapshot(ledger,std::move(groups),travel,now,0,capacity);
    apply_preferences(snapshot.materials,preferences); return snapshot;
}
void Account::remove_saved(std::uint64_t id) {
    if (busy_.exchange(true)) throw std::runtime_error("Planters is using these timers. Try again after its current run.");
    try {
        auto ledger=saved();
        const bool pending=ledger.pending && ledger.pending->plant.id==id;
        const auto removed=std::erase_if(ledger.plants,[&](const Plant& plant) { return plant.id==id; });
        if (!removed && !pending) throw std::invalid_argument("Saved planter no longer exists");
        if (pending) ledger.pending.reset();
        validate(ledger); store_->save(ledger);
        publish(ledger);
        ++edit_revision_;
    } catch (...) { busy_=false; throw; }
    busy_=false;
}
void Account::set_saved(std::optional<std::uint64_t> id,const Stats& stats,double progress,WallTime now) {
    if (busy_.exchange(true)) throw std::runtime_error("Planters is using these timers. Try again after its current run.");
    try {
        if (!std::isfinite(progress) || progress<0 || progress>1) throw std::invalid_argument("Invalid planter progress");
        const auto known=catalog();
        const auto match=std::find_if(known.begin(),known.end(),[&](const Stats& value) { return value.planter==stats.planter && value.field==stats.field; });
        if (match==known.end()) throw std::invalid_argument("Unknown planter and field combination");
        auto ledger=saved();
        if (id && ledger.pending && ledger.pending->plant.id==*id) {
            if (ledger.pending->operation==Operation::place) ledger.plants.push_back(ledger.pending->plant);
            ledger.pending.reset();
        }
        const auto duration=Time(static_cast<Time::rep>(std::max(std::ceil(match->full_seconds*1000),std::round(match->full_seconds)*1000)));
        const auto placed=now-Time(static_cast<Time::rep>(std::floor(progress*duration.count())));
        if (placed.time_since_epoch()<Time::zero()) throw std::invalid_argument("Invalid planter placement time");
        Plant plant{id.value_or(ledger.next_ticket),*match,Phase::build,placed,placed+duration,GrowthSample{now,progress}};
        if (id) {
            const auto found=std::find_if(ledger.plants.begin(),ledger.plants.end(),[&](const Plant& value) { return value.id==*id; });
            if (found==ledger.plants.end()) throw std::invalid_argument("Saved planter no longer exists");
            if (found->stats.planter==stats.planter && found->stats.field==stats.field) {
                plant.phase=found->phase;
                plant.due=placed+Time(static_cast<Time::rep>(std::ceil(harvest_fraction(*found)*duration.count())));
            }
            *found=plant;
        } else {
            ledger.plants.push_back(plant); ++ledger.next_ticket;
            // Adding a previously untracked planter turns one reserved slot into
            // a managed slot; collecting it later can safely free that slot.
            if (ledger.placement_limits.capacity && ++*ledger.placement_limits.capacity>=3) ledger.placement_limits.capacity.reset();
        }
        ledger.placement_limits.occupied_fields.erase(stats.field);
        validate(ledger); store_->save(ledger);
        publish(ledger);
        ++edit_revision_;
    } catch (...) { busy_=false; throw; }
    busy_=false;
}
void Account::publish(const Ledger& ledger) {
    std::lock_guard lock(snapshot_mutex_); plants_=ledger.plants; materials_=ledger.materials; placement_limits_=ledger.placement_limits;
}
void Account::prepare_groups(std::vector<Group>& groups,const std::optional<Preferences>& preferences) {
    std::lock_guard lock(snapshot_mutex_);
    for (auto& group:groups) {
        const auto remembered=nectar_phases_.at(static_cast<std::size_t>(group.nectar));
        if (remembered) group.phase=*remembered;
    }
    if (preferences) apply_preferences(groups,*preferences);
    for (auto& group:groups) {
        group.phase=group.current_known?planters::phase(group.phase,group.current,group.target):Phase::build;
        if (group.current_known) nectar_phases_.at(static_cast<std::size_t>(group.nectar))=group.phase;
    }
}
}
