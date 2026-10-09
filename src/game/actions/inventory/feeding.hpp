#pragma once
#include "core/runtime/program.hpp"
#include "game/detection/hive_view.hpp"

namespace hive::automation {
struct FeedingPlan {
    std::string food;
    unsigned amount{100};
    ProgramFactory prepare;
    std::shared_ptr<const game::InventorySource> inventory;
    std::shared_ptr<const game::DetectionSource> hive;
    std::shared_ptr<const game::LandmarkSource> reference;
};
std::unique_ptr<Program> feed_bees(FeedingPlan plan);
struct FeedReceipt { bool completed{}; unsigned amount{}; };
std::shared_ptr<const game::InventorySource> inventory_target(std::shared_ptr<const game::InventorySource> source,WindowPoint target);
std::unique_ptr<Program> feed_item(std::string food,unsigned amount,std::shared_ptr<const game::InventorySource> inventory,
    std::shared_ptr<FeedReceipt> receipt,Time settle=Time(800));
}
