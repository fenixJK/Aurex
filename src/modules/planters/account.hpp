#pragma once
#include "controller.hpp"
#include "planning.hpp"
#include "preferences.hpp"
#include <atomic>
#include <mutex>

namespace hive::features::planters {
class Execution;
class Account {
    friend class Execution;
    std::shared_ptr<Store> store_;
    std::atomic_bool busy_{};
    std::atomic_uint64_t edit_revision_{};
    AdaptivePlanner planner_;
    std::array<std::optional<Phase>,5> nectar_phases_;
    mutable std::mutex snapshot_mutex_;
    std::vector<Plant> plants_;
    MaterialHistory materials_;
    PlacementLimits placement_limits_;
    std::optional<WallTime> placement_retry_;
    std::optional<WallTime> offline_since_;
    std::vector<std::pair<WallTime,WallTime>> offline_;
    void apply_offline(Controller& controller);
    void publish(const Ledger& ledger);
    void prepare_groups(std::vector<Group>& groups,const std::optional<Preferences>& preferences);
public:
    explicit Account(std::shared_ptr<Store> store);
    std::vector<Plant> plants() const;
    MaterialHistory materials() const;
    std::size_t placement_capacity(std::size_t configured) const;
    void retry_placements();
    void defer_placements(WallTime now);
    std::optional<WallTime> placement_retry() const;
    bool resume_placements(WallTime now);
    Ledger saved() const;
    PlanningSnapshot forecast_state(std::vector<Group> groups,const Preferences& preferences,WallTime now,std::size_t capacity) const;
    bool busy() const { return busy_.load(); }
    std::uint64_t edit_revision() const { return edit_revision_.load(); }
    void remove_saved(std::uint64_t id);
    void set_saved(std::optional<std::uint64_t> id,const Stats& stats,double progress,WallTime now);
    void connection(bool reconnecting,WallTime now);
};
}
