#include "hive/automation/booster_routes.hpp"
#include "route_helpers.hpp"

namespace hive::automation {
std::shared_ptr<const workflows::RouteCatalog> walking_booster_routes(double speed,std::shared_ptr<const movement::Recipes> recipes) {
    if (!recipes) recipes=movement::snapshot();
    if (!std::isfinite(speed) || speed<8 || speed>200) throw std::invalid_argument("Booster routes need a valid speed");
    std::vector<workflows::ExecutableRoute> routes;
    for (unsigned slot=1;slot<=6;++slot) for (bool blue:{false,true}) {
        auto path=recipes->path("boosters",{{"slot",int(slot)},{"blue",blue},{"cannon",false}},speed);
        const std::string location=blue?"station.blue-booster":"station.red-booster";
        path.push_back({location,{},{},Time(5000),Time(2000),0});
        Time timeout{1500}; for (const auto& step:path) timeout+=step.timeout;
        const auto estimate=detail::estimate(path)+Time(1500);
        routes.push_back({{"hive-"+std::to_string(slot)+(blue?"-blue-booster":"-red-booster"),"hive.proximity",location,estimate,
            {"hive.slot."+std::to_string(slot)},game::RouteArrival::observed,{"hive.camera.prepared"},timeout},std::move(path)});
    }
    auto locations=detail::locations(routes);
    return std::make_shared<workflows::RouteCatalog>(std::move(locations),std::move(routes));
}
std::shared_ptr<const workflows::RouteCatalog> shortcut_booster_routes(double speed,std::shared_ptr<const movement::Recipes> recipes) {
    if (!recipes) recipes=movement::snapshot();
    if (!std::isfinite(speed) || speed<8 || speed>200) throw std::invalid_argument("Booster routes need a valid speed");
    std::vector<workflows::ExecutableRoute> routes;
    for (unsigned slot=1;slot<=6;++slot) for (bool blue:{false,true}) {
        auto path=recipes->path("boosters",{{"slot",int(slot)},{"blue",blue},{"cannon",true}},speed);
        const std::string location=blue?"station.blue-booster":"station.red-booster";
        path.push_back({location,{},{},Time(5000),Time(2000),0});
        Time timeout{1500}; for (const auto& step:path) timeout+=step.timeout;
        const auto estimate=detail::estimate(path)+Time(1500);
        routes.push_back({{"hive-"+std::to_string(slot)+(blue?"-blue-booster-shortcut":"-red-booster-shortcut"),"hive.proximity",location,estimate,
            {"hive.slot."+std::to_string(slot)},game::RouteArrival::observed,{"hive.camera.prepared"},timeout},std::move(path)});
    }
    auto locations=detail::locations(routes);
    return std::make_shared<workflows::RouteCatalog>(std::move(locations),std::move(routes));
}
}
