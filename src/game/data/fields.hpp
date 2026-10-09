#pragma once
#include <span>
#include <string>
#include <string_view>

namespace hive::game {
struct FieldDefinition {
    std::string id,name;
    unsigned minimum_bees{};
    std::string location() const { return id+".entry"; }
    std::string planting_location() const { return id+".planting"; }
};
std::span<const FieldDefinition> fields();
const FieldDefinition* field_at(std::string_view location);
}
