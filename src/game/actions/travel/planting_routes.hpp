#pragma once
#include "core/scripting/movement.hpp"
#include "navigation.hpp"

namespace hive::automation {
std::shared_ptr<const workflows::RouteCatalog> walking_planting_routes(double speed,std::shared_ptr<const movement::Recipes> recipes={});
}
