#pragma once
#include "tracker.hpp"
#include <span>

namespace hive::features::quests {
struct Requirement {
    std::string action,target,field;
    game::PetalColor color{game::PetalColor::any};
};
struct QuestDefinition { std::string giver,name; std::vector<Requirement> rows; };
std::span<const QuestDefinition> quest_catalog();
const QuestDefinition* identify_quest(const std::string& title,const std::string& giver={});
std::optional<QuestDefinition> resolve_quest(const std::string& title,const std::string& giver={});
Objective catalog_objective(const QuestDefinition& quest,std::size_t row,std::string id,bool complete);
}
