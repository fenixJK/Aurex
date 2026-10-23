#pragma once
#include "execution.hpp"
#include "game/detection/localization.hpp"

namespace hive::features::planters {
std::shared_ptr<const Actions> desktop_actions(const ExecutionPlan& plan,
    std::shared_ptr<const game::InventorySource> inventory,std::shared_ptr<const game::LandmarkSource> harvest_prompt);
struct PlanterControls {
    std::vector<std::string> inventory_names;
};
struct FieldAccess {
    std::string target;
    std::shared_ptr<const game::InventorySource> source;
    std::function<workflows::CheckpointPlan()> harvest_approach;
};
std::shared_ptr<const Actions> observed_actions(std::map<std::string,PlanterControls> controls,std::map<std::string,FieldAccess> fields,
    std::shared_ptr<const game::LandmarkSource> harvest_prompt={});
}
