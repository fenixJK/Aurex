#pragma once
#include "plan.hpp"
#include "modules/shared/visit_cooldowns.hpp"
#include "game/data/item_stock.hpp"
#include "coordination/attack.hpp"

namespace hive::features::combat {
coordination::AttackAssist attack_assist(Plan plan,std::shared_ptr<const game::RouteNetwork> network,
    std::shared_ptr<const automation::AvailabilitySource> availability,std::shared_ptr<visits::Cooldowns> saved,
    std::shared_ptr<game::ItemStock> stock,std::shared_ptr<const game::InventorySource> inventory);
}
