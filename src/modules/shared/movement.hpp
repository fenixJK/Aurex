#pragma once
#include "hive/scripting/movement.hpp"
#include "hive/automation/program.hpp"

namespace hive::features {
inline std::vector<automation::Walk> movement_walks(const movement::Recipes& recipes,const std::string& name,
    const movement::Parameters& parameters={},bool tool=false) {
    std::vector<automation::Walk> result;
    for (const auto& leg:recipes.legs(name,parameters)) result.push_back({leg,tool});
    return result;
}
}
