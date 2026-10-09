#pragma once
#include "game/detection/landmarks.hpp"
#include <span>

namespace hive::game {
struct Npc {
    std::string id,name;
    std::vector<std::string> prompts;
    Time quest_interval{};
    std::string location() const { return "npc."+id; }
    PromptLandmark prompt() const { auto phrases=prompts; phrases.push_back(name); return {std::move(phrases),{.2,0,.6,.3},.5f,PromptMatch::phrase,true}; }
};
std::span<const Npc> npcs();
const Npc& npc(const std::string& name);
}
