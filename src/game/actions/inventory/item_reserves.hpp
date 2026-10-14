#pragma once
#include "core/runtime/program.hpp"
#include "game/data/item_stock.hpp"

namespace hive::automation {
std::unique_ptr<Program> inspect_reserves(std::shared_ptr<game::ItemStock> stock,
    std::shared_ptr<const game::InventorySource> inventory,game::ItemAmounts cost);
}
