#pragma once
#include "planner.hpp"

namespace hive::features::planters {
struct Equipment { std::string id,label; };
struct Preferences {
    std::set<std::string> planters,fields;
    std::array<Target,5> targets;
    std::array<int,5> priorities{1,2,3,4,5};
};
std::span<const Equipment> equipment();
std::span<const std::string> nectar_names();
Preferences default_preferences();
void validate(const Preferences& preferences);
void apply_preferences(std::vector<Group>& groups,const Preferences& preferences);
}
