#include "item_routes.hpp"
#include "route_helpers.hpp"

namespace hive::automation {
std::shared_ptr<const workflows::RouteCatalog> item_routes(double speed,bool cannon,std::shared_ptr<const movement::Recipes> recipes) {
    if (!recipes) recipes=movement::snapshot();
    if (!std::isfinite(speed) || speed<8 || speed>200) throw std::invalid_argument("Item activity routes need a valid speed");
    std::vector<workflows::ExecutableRoute> routes;
    for (unsigned slot=1;slot<=6;++slot) for (const auto kind:{"blender","wind-shrine","coconut","glue","robo-pass","honeystorm","mountain-booster","sticker-printer"}) {
        const bool blender=std::string_view(kind)=="blender";
        if (cannon && (std::string_view(kind)=="wind-shrine" || std::string_view(kind)=="coconut")) continue;
        auto path=recipes->path(blender || std::string_view(kind)=="wind-shrine"?"items":"activities",{{"kind",std::string(kind)},{"slot",int(slot)},{"cannon",cannon}},speed);
        const auto destination=std::string("station.")+kind;
        const bool portal=std::string_view(kind)=="glue";
        if (!portal) path.push_back({destination,{},{},Time(5000),Time(2000),0});
        Time timeout{1500}; for (const auto& checkpoint:path) timeout+=checkpoint.timeout;
        routes.push_back({{"hive-"+std::to_string(slot)+"-"+(blender || std::string_view(kind)=="wind-shrine"?"":"activity-")+kind+(cannon?"-cannon":""),"hive.proximity",destination,detail::estimate(path)+Time(1500),
            {"hive.slot."+std::to_string(slot)},portal?game::RouteArrival::estimated:game::RouteArrival::observed,{"hive.camera.prepared"},timeout},std::move(path)});
    }
    auto locations=detail::locations(routes);
    return std::make_shared<workflows::RouteCatalog>(std::move(locations),std::move(routes));
}
}
