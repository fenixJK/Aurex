#pragma once
#include "hive/scripting/movement.hpp"
#include "hive/workflows/navigation.hpp"

namespace hive::automation {
struct TravelAccess { unsigned bees{50}; bool cannon{true}; };
std::shared_ptr<const workflows::RouteCatalog> live_hive_routes(double speed,TravelAccess access,movement::Source source,bool walking_only=false);
std::shared_ptr<const workflows::RouteCatalog> accessible_routes(const workflows::RouteCatalog& routes,TravelAccess access);
std::shared_ptr<const workflows::RouteCatalog> walking_hive_routes(double speed,TravelAccess access={},std::shared_ptr<const movement::Recipes> recipes={});
std::shared_ptr<const workflows::RouteCatalog> hive_routes(double speed,TravelAccess access={},std::shared_ptr<const movement::Recipes> recipes={});
}
