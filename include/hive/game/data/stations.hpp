#pragma once
#include "../detection/text.hpp"

namespace hive::game {
enum class StationInteraction { activate, memory_match, blender, wind_shrine, sticker_printer };
struct StationDefinition {
    std::string name,location;
    Time cooldown;
    TextTarget prompt;
    Time settle{};
    bool selected_by_default{true};
    StationInteraction interaction{StationInteraction::activate};
    bool night_only{};
    std::vector<std::string> boosted_fields;
};
const std::vector<StationDefinition>& stations();
TextTarget station_target(const std::string& name,bool cooldown=false);
}
