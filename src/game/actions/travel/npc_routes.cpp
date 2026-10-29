#include "npc_routes.hpp"
#include "game/data/npcs.hpp"
#include "route_helpers.hpp"

namespace hive::automation {
std::shared_ptr<const workflows::RouteCatalog> walking_npc_routes(double speed,std::shared_ptr<const movement::Recipes> recipes) {
    if (!recipes) recipes=movement::snapshot();
    if (!std::isfinite(speed) || speed<8 || speed>200) throw std::invalid_argument("NPC routes need a valid movement speed");
    std::vector<workflows::ExecutableRoute> routes;
    for (const auto& npc:game::npcs()) for (unsigned slot=1;slot<=6;++slot) {
        auto path=recipes->path("npc",{{"name",npc.name},{"slot",int(slot)}},speed);
        // The completed NPC route supplies identity; E confirms interaction range.
        path.push_back({"interaction.prompt",{},{},Time(5000),Time(2000),0});
        Time timeout{1500}; for (const auto& step:path) timeout+=step.timeout;
        const auto estimate=detail::estimate(path)+Time(1500);
        routes.push_back({{"hive-"+std::to_string(slot)+"-"+npc.id,"hive.proximity",npc.location(),estimate,
            {"hive.slot."+std::to_string(slot)},game::RouteArrival::estimated,{"hive.camera.prepared"},timeout},std::move(path)});
    }
    auto locations=detail::locations(routes);
    return std::make_shared<workflows::RouteCatalog>(std::move(locations),std::move(routes));
}
}
