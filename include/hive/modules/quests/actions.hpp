#pragma once
#include "cycle.hpp"
#include "hive/scripting/movement.hpp"
#include "hive/game/detection/inventory.hpp"
#include "hive/game/movement/navigation.hpp"

namespace hive::features::quests {
void configure_actions(CyclePlan& plan,const game::RouteNetwork& routes,
    std::shared_ptr<const game::InventorySource> inventory,std::shared_ptr<const game::DetectionSource> hive_view,
    std::shared_ptr<const game::LandmarkSource> reference,std::shared_ptr<const game::DetectionSource> blooms,movement::Source movement={},std::shared_ptr<game::Cooldowns> collection_cooldowns={},
    std::function<automation::ProgramFactory(const std::string&)> collect_station={},std::shared_ptr<const BrowserSource> quests={});
}
