#include "planting_routes.hpp"
#include "route_helpers.hpp"

namespace hive::automation {
std::shared_ptr<const workflows::RouteCatalog> walking_planting_routes(double speed,std::shared_ptr<const movement::Recipes> recipes) {
    if (!std::isfinite(speed) || speed<8 || speed>200) throw std::invalid_argument("Planting routes need a valid movement speed");
    if (!recipes) recipes=movement::snapshot();
    std::vector<workflows::ExecutableRoute> routes;
    for (unsigned slot=1;slot<=6;++slot) {
        const auto add=[&](const std::string& field) {
            auto path=recipes->path("planting",{{"field",field},{"slot",int(slot)}},speed);
            Time timeout{1500}; for (const auto& checkpoint : path) timeout+=checkpoint.timeout;
            routes.push_back({{"hive-"+std::to_string(slot)+"-plant-"+field,"hive.proximity",field+".planting",
                detail::estimate(path)+Time(1500),{"hive.slot."+std::to_string(slot)},game::RouteArrival::estimated,
                {"hive.camera.prepared"},timeout},std::move(path)});
        };
        for (const auto* field:{"coconut","pepper","sunflower","dandelion","mushroom","blueflower","clover","spider","strawberry","pineapple","bamboo","cactus","pinetree","mountaintop","rose","stump","pumpkin"}) add(field);
    }
    auto locations=detail::locations(routes);
    return std::make_shared<workflows::RouteCatalog>(std::move(locations),std::move(routes));
}
}
