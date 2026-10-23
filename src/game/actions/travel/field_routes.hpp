#pragma once
#include "core/scripting/movement.hpp"
#include "navigation.hpp"
#include "core/runtime/program.hpp"
#include "services.hpp"
#include "hive_routes.hpp"

namespace hive::automation {
class HiveSetupResult;
struct FieldReturnOptions {
    std::shared_ptr<HiveSetupResult> hive;
    TravelAccess access{50,false};
    std::shared_ptr<const game::LandmarkSource> transport_prompt;
    movement::Source movement;
};
std::shared_ptr<const workflows::RouteCatalog> cannon_field_routes(double speed,std::shared_ptr<const movement::Recipes> recipes={});
std::shared_ptr<const workflows::RouteCatalog> sunflower_routes(unsigned hive_slot,double speed,std::shared_ptr<const movement::Recipes> recipes={});
std::shared_ptr<const workflows::RouteCatalog> walking_field_routes(unsigned hive_slot,double speed,std::shared_ptr<const movement::Recipes> recipes={});
std::shared_ptr<const workflows::RouteCatalog> walking_field_routes(double speed,std::shared_ptr<const movement::Recipes> recipes={});
std::shared_ptr<const NavigationPerception> hive_navigation(std::shared_ptr<const NavigationPerception> perception,
    std::shared_ptr<HiveSetupResult> setup);
std::unique_ptr<Program> return_from_field(const std::string& field,double speed,unsigned hive_slot=0,FieldReturnOptions options={});
std::unique_ptr<Program> return_from_planting(const std::string& field,double speed,unsigned hive_slot=0,FieldReturnOptions options={});
}
