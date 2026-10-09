#include "npcs.hpp"

namespace hive::game {
std::span<const Npc> npcs() {
    static const Npc values[]{
        {"black-bear","Black Bear",{},std::chrono::hours(1)},{"brown-bear","Brown Bear",{},std::chrono::hours(1)},{"polar-bear","Polar Bear"},
        {"honey-bee","Honey Bee"},{"bucko-bee","Bucko Bee",{"Talk to Gifted Bucko Bee"}},{"riley-bee","Riley Bee",{"Talk to Gifted Riley Bee"}}};
    return values;
}
const Npc& npc(const std::string& name) {
    for (const auto& value:npcs()) if (value.name==name) return value;
    throw std::invalid_argument("Unknown NPC");
}
}
