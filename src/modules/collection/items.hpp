#pragma once
#include "core/runtime/program.hpp"
#include "game/detection/item_menu.hpp"
#include "orders.hpp"
#include "ant_pass.hpp"
#include "core/scripting/movement.hpp"

namespace hive::features::collection {
struct ItemOptions {
    std::string recipe{"Red Extract"},donation{"Cloud Vial"};
    unsigned batches{1},quantity{1};
    std::shared_ptr<ItemOrders> orders;
    std::shared_ptr<game::ItemStock> stock;
    std::shared_ptr<const game::InventorySource> inventory;
    Time shrine_interval{std::chrono::hours(1)};
    movement::Source movement;
    std::string printer_egg{"Basic Egg"};
    std::shared_ptr<AntPassBudget> ant_passes;
};
std::unique_ptr<automation::Program> print_sticker(std::shared_ptr<const game::DetectionSource> source,std::string egg);
std::unique_ptr<automation::Program> collect_glue(std::shared_ptr<const game::InventorySource> inventory,double speed,movement::Source movement);
struct ItemResult { std::optional<Time> retry,cooldown; };
std::unique_ptr<automation::Program> activate_sticker_stack(std::shared_ptr<const game::DetectionSource> source,
    std::shared_ptr<ItemResult> result);
std::unique_ptr<automation::Program> blend(std::shared_ptr<const game::DetectionSource> source,
    ItemOptions options,std::shared_ptr<ItemResult> result);
std::unique_ptr<automation::Program> donate(std::shared_ptr<const game::DetectionSource> source,
    ItemOptions options,std::shared_ptr<ItemResult> result,double speed);
}
