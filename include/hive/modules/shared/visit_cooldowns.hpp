#pragma once
#include "hive/automation/visits.hpp"
#include "hive/game/data/cooldowns.hpp"

namespace hive::features::visits {
using game::WallTime;
using game::WallClock;
using game::Cooldown;
using game::CooldownRecords;
using game::CooldownStore;
using game::Cooldowns;
using game::validate;
std::shared_ptr<const automation::AvailabilitySource> saved_availability(std::shared_ptr<const automation::AvailabilitySource> source,
    std::shared_ptr<Cooldowns> saved,const automation::VisitPlan& plan);
std::unique_ptr<automation::Program> save_cooldowns(automation::VisitRun run,std::shared_ptr<Cooldowns> saved);
}
