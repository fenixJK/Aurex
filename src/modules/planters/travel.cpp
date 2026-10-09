#include "execution.hpp"
#include "game/data/fields.hpp"
#include "modules/shared/movement.hpp"
#include "game/actions/travel/field_routes.hpp"

namespace hive::features::planters {
ExecutionPlan walking_plan(const workflows::RouteCatalog& routes,double speed,automation::ProgramFactory return_to_hive,
    automation::FieldReturnOptions returns) {
    if (!std::isfinite(speed) || speed<8 || speed>200) throw std::invalid_argument("Walking Planters needs a valid movement speed");
    ExecutionPlan plan; plan.config.speed=speed; plan.movement=returns.movement;
    if (!routes.network().contains("hive.proximity")) return plan;
    plan.return_to="hive.proximity"; plan.walk_return=true;
    plan.preferences=default_preferences(); plan.preferences->fields.clear();
    for (const auto& field : game::fields()) {
        bool outbound=false;
        for (const auto& route : routes.network().routes())
            outbound|=route.from==*plan.return_to && route.to==field.planting_location();
        if (!outbound) continue;
        plan.destinations.emplace(field.name,field.planting_location());
        plan.preferences->fields.insert(field.name);
        plan.visits.emplace(field.name,FieldVisit{return_to_hive,
            movement_walks(*movement::snapshot(returns.movement),"planter-loot",{{"field",field.name}}),
            [id=field.id,speed,returns] { return automation::return_from_planting(id,speed,0,returns); }});
    }
    return plan;
}
}
