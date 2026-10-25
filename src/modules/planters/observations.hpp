#pragma once
#include "execution.hpp"
#include "game/detection/nectar.hpp"
#include "game/detection/localization.hpp"

namespace hive::features::planters {
game::FixedLabelQuery placement_limit_query();
Observation<bool> placement_limit(const automation::View& view);
Observation<HarvestEvidence> harvest_receipt(const Stats& planter,bool had_growth_bar,
    const Snapshot& after,const automation::View& view,Time minimum);
std::shared_ptr<const Source> observed_source(std::shared_ptr<const game::NectarSource> nectar,
    std::shared_ptr<const game::LandmarkSource> harvest_prompt,std::shared_ptr<const game::LandmarkSource> placement_visible,
    std::shared_ptr<const game::LandmarkSource> growth_bar);
}
