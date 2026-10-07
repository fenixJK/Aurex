#include "hive/game/data/stations.hpp"

namespace hive::game {
const std::vector<StationDefinition>& stations() {
    using namespace std::chrono;
    static const std::vector<StationDefinition> catalog{
        {"Red Field Booster","station.red-booster",minutes(45),{"Red Field Booster",{.2,0,.6,.3},false,true},Time{},false,
            StationInteraction::activate,false,{"Mushroom","Strawberry","Rose","Pepper"}},
        {"Blue Field Booster","station.blue-booster",minutes(45),{"Blue Field Booster",{.2,0,.6,.3},false,true},Time{},false,
            StationInteraction::activate,false,{"Blue Flower","Bamboo","Pine Tree","Stump"}},
        {"Mountain Top Field Booster","station.mountain-booster",hours(1),{"Field Booster",{.2,0,.6,.3},false,true},Time{},false,
            StationInteraction::activate,false,{"Sunflower","Dandelion","Spider","Clover","Pineapple","Pumpkin","Cactus"}},
        {"Coconut Dispenser","station.coconut",hours(4),{"Coconut Dispenser",{.2,0,.6,.3},false,true},Time{},false,
            StationInteraction::activate,false,{"Coconut"}},
        {"Glue Dispenser","station.glue",hours(22),{"Glue Dispenser",{.2,0,.6,.3},false,true},Time{},false},
        {"Free Robo Pass Dispenser","station.robo-pass",hours(22),{"Free Robo Pass Dispenser",{.2,0,.6,.3},false,true},Time{},false},
        {"Honeystorm","station.honeystorm",hours(4),{"Summon Honeystorm",{.2,0,.6,.3},false,true},Time{},false},
        {"Sticker Printer","station.sticker-printer",hours(1),{"Sticker Printer",{.2,0,.6,.3},false,true},Time{},false,StationInteraction::sticker_printer},
        {"Wealth Clock","station.clock",hours(1),{"Wealth Clock",{.2,0,.6,.3},false,true}},
        {"Honey Dispenser","station.honey",hours(1),{"Honey Dispenser",{.2,0,.6,.3},false,true}},
        {"Treat Dispenser","station.treats",hours(1),{"Treat Dispenser",{.2,0,.6,.3},false,true}},
        {"Blueberry Dispenser","station.blueberries",hours(4),{"Blueberry Dispenser",{.2,0,.6,.3},false,true}},
        {"Strawberry Dispenser","station.strawberries",hours(4),{"Strawberry Dispenser",{.2,0,.6,.3},false,true}},
        {"Free Royal Jelly Dispenser","station.royal-jelly",hours(22),
            {"Claim 1 Royal Jelly per Gifted Bee",{.2,0,.6,.3},false,true,CountedSuffix{"Royal Jelly","Royal Jellies",50}},seconds(10)},
        {"Free Ant Pass Dispenser","station.ant-pass",hours(2),{"Free Ant Pass Dispenser",{.2,0,.6,.3},false,true},Time{},false},
        {"Memory Match","station.memory-basic",hours(2),{"Spend 25,000 Honey to Play Memory Match",{.2,0,.6,.3},false,true},Time{},false,StationInteraction::memory_match},
        {"Mega Memory Match","station.memory-mega",hours(4),{"Mega Memory Match",{.2,0,.6,.3},false,true},Time{},false,StationInteraction::memory_match},
        {"Night Memory Match","station.memory-night",hours(8),{"Night Memory Match",{.2,0,.6,.3},false,true},Time{},false,StationInteraction::memory_match,true},
        {"Extreme Memory Match","station.memory-extreme",hours(8),{"Extreme Memory Match",{.2,0,.6,.3},false,true},Time{},false,StationInteraction::memory_match},
        {"Blender","station.blender",minutes(5),{"Blender",{.2,0,.6,.3},false,true},Time{},false,StationInteraction::blender},
        {"Wind Shrine","station.wind-shrine",hours(1),{"Wind Shrine",{.2,0,.6,.3},false,true},Time{},false,StationInteraction::wind_shrine}
    };
    return catalog;
}
TextTarget station_target(const std::string& name,bool cooldown) {
    TextTarget target{name,{.2,0,.6,.3},cooldown,true};
    for (const auto& definition : stations()) if (definition.name==name) { target=definition.prompt; target.cooldown=cooldown; break; }
    validate(target); return target;
}
}
