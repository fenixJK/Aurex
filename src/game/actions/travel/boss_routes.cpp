#include "boss_routes.hpp"
#include "route_helpers.hpp"
namespace hive::automation {
std::shared_ptr<const workflows::RouteCatalog> boss_routes(double speed,std::shared_ptr<const movement::Recipes> recipes) {
    if (!recipes) recipes=movement::snapshot();
    std::vector<workflows::ExecutableRoute> routes;
    for (const auto& [id,name]:std::map<std::string,std::string>{{"king-beetle","King Beetle"},{"tunnel-bear","Tunnel Bear"},{"commando","Commando Chick"}})
        for (unsigned slot=1;slot<=6;++slot) {
            auto path=recipes->path("bosses",{{"enemy",name},{"slot",int(slot)}},speed);
            Time timeout{1500}; for (const auto& part:path) timeout+=part.timeout;
            routes.push_back({{"hive-"+std::to_string(slot)+"-"+id,"hive.proximity","boss."+id,detail::estimate(path)+Time(1500),
                {"hive.slot."+std::to_string(slot)},game::RouteArrival::estimated,{"hive.camera.prepared"},timeout},std::move(path)});
        }
    auto locations=detail::locations(routes);
    return std::make_shared<workflows::RouteCatalog>(std::move(locations),std::move(routes));
}
}
