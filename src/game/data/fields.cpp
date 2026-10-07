#include "hive/game/data/fields.hpp"

namespace hive::game {
std::span<const FieldDefinition> fields() {
    static const FieldDefinition values[]{
        {"bamboo","Bamboo",5},{"blueflower","Blue Flower"},
        {"cactus","Cactus",15},{"clover","Clover"},{"coconut","Coconut",35},{"dandelion","Dandelion"},
        {"mountaintop","Mountain Top",25},{"mushroom","Mushroom"},{"pepper","Pepper",35},
        {"pinetree","Pine Tree",15},{"pineapple","Pineapple",10},{"pumpkin","Pumpkin",15},
        {"rose","Rose",15},{"spider","Spider",5},{"strawberry","Strawberry",5},
        {"stump","Stump",10},{"sunflower","Sunflower"}};
    return values;
}
const FieldDefinition* field_at(std::string_view location) {
    for (const auto& field : fields()) if (field.location()==location) return &field;
    return nullptr;
}
}
