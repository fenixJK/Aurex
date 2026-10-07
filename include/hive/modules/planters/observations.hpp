#pragma once
#include "execution.hpp"
#include "hive/game/detection/nectar.hpp"
#include "hive/game/detection/planters.hpp"

namespace hive::features::planters {
Observation<HarvestEvidence> harvest_receipt(const Stats& planter,const Observation<game::NectarLevels>& before,
    const Snapshot& after,const ObservationContext& context,Time minimum);
Observation<HarvestEvidence> reward_receipt(const Stats& planter,const Observation<std::vector<game::PlanterReward>>& before,
    const Snapshot& after,const ObservationContext& context,Time minimum);
std::shared_ptr<const Source> interaction_source(std::shared_ptr<const game::NectarSource> nectar,std::shared_ptr<const game::PlanterSource> planters);
std::shared_ptr<const Source> observed_source(std::shared_ptr<const game::NectarSource> nectar,std::shared_ptr<const Source> field,
    std::shared_ptr<const game::PlanterSource> planters={});
}
