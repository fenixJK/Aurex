#pragma once
#include "core/runtime/model.hpp"
#include <string_view>

namespace hive::features::gathering {
struct FieldDefaults {
    std::string_view name,pattern;
    int width,scale;
    Point start;
    unsigned heading;
    bool invert_lr;
};
inline constexpr FieldDefaults field_defaults[]{
    {"Sunflower","CornerXSnake",4,100,{-7.2,4.5},0,true},
    {"Dandelion","CornerXSnake",6,100,{-4.5,11.3},0,true},
    {"Mushroom","CornerXSnake",2,100,{-6.3,4.5},0,true},
    {"Blue Flower","CornerXSnake",7,100,{},2,false},
    {"Clover","Stationary",1,75,{},0,false},
    {"Spider","CornerXSnake",1,100,{-5.4,5.4},0,true},
    {"Strawberry","CornerXSnake",1,100,{5.4,4.1},2,true},
    {"Bamboo","CornerXSnake",3,100,{-2.3,5.4},6,true},
    {"Pineapple","CornerXSnake",1,100,{-10.8,6.3},0,true},
    {"Stump","Stationary",1,75,{},0,false},
    {"Cactus","Stationary",1,75,{},0,false},
    {"Pumpkin","CornerXSnake",5,100,{9,0},2,true},
    {"Pine Tree","CornerXSnake",3,100,{-5.5,7.9},6,false},
    {"Rose","CornerXSnake",1,100,{6.8,-11.3},4,true},
    {"Mountain Top","CornerXSnake",3,100,{-3.4,-5.1},4,false},
    {"Coconut","CornerXSnake",3,100,{6.1,0},2,true},
    {"Pepper","CornerXSnake",5,100,{7.1,4.7},0,false}
};
inline const FieldDefaults* defaults_for(std::string_view name) {
    for (const auto& field:field_defaults) if (field.name==name) return &field;
    return nullptr;
}
}
