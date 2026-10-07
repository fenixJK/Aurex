#pragma once
#include "hive/scripting/movement.hpp"
#include "hive/workflows/navigation.hpp"

namespace hive::automation {
std::shared_ptr<const workflows::RouteCatalog> memory_routes(double speed,bool cannon=false,std::shared_ptr<const movement::Recipes> recipes={});
}
