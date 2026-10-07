#include "hive/automation/field_routes.hpp"
#include "route_helpers.hpp"

namespace hive::automation {
std::shared_ptr<const workflows::RouteCatalog> cannon_field_routes(double speed,std::shared_ptr<const movement::Recipes> recipes) {
    if (!recipes) recipes=movement::snapshot();
    if (!std::isfinite(speed) || speed<8 || speed>200) throw std::invalid_argument("Cannon routes require a valid speed");
    std::vector<workflows::ExecutableRoute> routes;
    for (unsigned slot=1;slot<=6;++slot) for (bool planting:{false,true})
        for (const std::string field:{"mountaintop","pinetree","cactus","pumpkin","pineapple","stump","rose","bamboo","blueflower","clover","spider","strawberry"}) {
            auto path=recipes->path("cannon",{{"field",field},{"planting",planting},{"slot",int(slot)}},speed);
            Time timeout{1500}; for (const auto& p:path) timeout+=p.timeout;
            const auto estimate=detail::estimate(path);
            const auto site=field+(planting?".planting":".entry");
            routes.push_back({{"cannon-"+std::to_string(slot)+"-"+site,"hive.proximity",site,estimate,
                {"hive.slot."+std::to_string(slot)},game::RouteArrival::estimated,{"hive.camera.prepared"},timeout},std::move(path)});
        }
    auto locations=detail::locations(routes);
    return std::make_shared<workflows::RouteCatalog>(std::move(locations),std::move(routes));
}
}
