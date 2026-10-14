#pragma once
#include "planner.hpp"

namespace hive::features::planters {
struct ItemGoal { std::string id,label,category; };
struct PlanterDrops {
    std::string planter,wiki;
    std::map<std::string,unsigned> guaranteed;
    std::vector<std::string> favored,other;
    std::string notes;
};
struct ItemSource { unsigned minimum{}; int preference{}; bool field_bonus{}; };
std::span<const ItemGoal> item_goals();
std::span<const PlanterDrops> planter_drops();
ItemSource item_source(const std::string& goal,const Stats& stats);
std::string item_label(const std::string& goal);
std::string drop_description(const PlanterDrops& drops);
}
