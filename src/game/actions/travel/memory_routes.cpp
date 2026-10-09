#include "memory_routes.hpp"
#include "route_helpers.hpp"

namespace hive::automation {
std::shared_ptr<const workflows::RouteCatalog> memory_routes(double speed,bool cannon,std::shared_ptr<const movement::Recipes> recipes) {
    if (!recipes) recipes=movement::snapshot();
    if (!std::isfinite(speed) || speed<8 || speed>200) throw std::invalid_argument("Memory Match routes need a valid movement speed");
    std::vector<workflows::ExecutableRoute> routes;
    for (unsigned slot=1;slot<=6;++slot) for (const std::string kind:{"basic","mega","night","extreme"}) {
        if (cannon && kind=="extreme") continue;
        auto path=recipes->path("memory",{{"kind",kind},{"slot",int(slot)},{"cannon",cannon}},speed);
        const auto destination="station.memory-"+kind;
        path.push_back({destination,{},{},Time(5000),Time(2000),0});
        Time timeout{1500}; for (const auto& step:path) timeout+=step.timeout;
        const auto duration=detail::estimate(path)+Time(1500);
        routes.push_back({{"hive-"+std::to_string(slot)+"-memory-"+kind+(cannon?"-cannon":""),"hive.proximity",destination,duration,
            {"hive.slot."+std::to_string(slot)},game::RouteArrival::observed,{"hive.camera.prepared"},timeout},std::move(path)});
    }
    auto locations=detail::locations(routes);
    return std::make_shared<workflows::RouteCatalog>(std::move(locations),std::move(routes));
}
}
