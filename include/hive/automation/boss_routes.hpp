#pragma once
#include "hive/scripting/movement.hpp"
namespace hive::workflows { class RouteCatalog; }
namespace hive::automation {
std::shared_ptr<const workflows::RouteCatalog> boss_routes(double speed,std::shared_ptr<const movement::Recipes> recipes={});
}
