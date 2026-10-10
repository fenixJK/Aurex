#pragma once
#include "results.hpp"
#include "mutations.hpp"
#include "core/runtime/program.hpp"
#include <mutex>

namespace hive::features::tools {
struct RollReport { unsigned attempts{}; std::string result,status{"Ready"}; bool matched{}; unsigned bitterberries{},neonberries{}; };
class RollProgress {
    mutable std::mutex mutex_;
    RollReport report_;
public:
    RollReport read() const { std::lock_guard lock(mutex_); return report_; }
    void set(RollReport report) { std::lock_guard lock(mutex_); report_=std::move(report); }
};
struct SsaPlan { AmuletFilter filter; unsigned honey_billions{500}; };
struct JellyPlan {
    BeeFilter filter;
    unsigned rolls{100};
    bool require_mutation{};
    MutationFilter mutations;
    bool neon{};
    unsigned neon_limit{1};
    std::optional<WindowPoint> target;
    std::shared_ptr<const game::InventorySource> inventory;
};
struct HatchPlan { WindowPoint target; std::shared_ptr<const game::InventorySource> inventory; unsigned eggs{100}; bool keep_gifted{true},keep_mythic{true},start_with_jelly{}; };
struct BitterPlan {
    WindowPoint target;
    std::shared_ptr<const game::InventorySource> inventory;
    MutationFilter mutations;
    unsigned batch{30},limit{1000},neon_limit{1};
    bool neon{};
};
std::unique_ptr<automation::Program> hatch_basic(HatchPlan plan,std::shared_ptr<RollProgress> progress);
std::unique_ptr<automation::Program> roll_ssa(SsaPlan plan,std::shared_ptr<RollProgress> progress);
std::unique_ptr<automation::Program> roll_jelly(JellyPlan plan,std::shared_ptr<RollProgress> progress);
std::unique_ptr<automation::Program> roll_mutations(BitterPlan plan,std::shared_ptr<RollProgress> progress);
}
