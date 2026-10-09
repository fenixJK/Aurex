#pragma once
#include "core/scripting/movement.hpp"
#include "navigation.hpp"

namespace hive::automation {
std::shared_ptr<const workflows::RouteCatalog> memory_routes(double speed,bool cannon=false,std::shared_ptr<const movement::Recipes> recipes={});
}
