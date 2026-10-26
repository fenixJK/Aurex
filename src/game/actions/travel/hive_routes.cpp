#include "hive_routes.hpp"
#include "game/data/fields.hpp"
#include "game/data/stations.hpp"
#include <mutex>
#include "field_routes.hpp"
#include "planting_routes.hpp"
#include "route_helpers.hpp"
#include "npc_routes.hpp"
#include "booster_routes.hpp"
#include "game/actions/activities/ant_challenge.hpp"
#include "memory_routes.hpp"
#include "item_routes.hpp"
#include "boss_routes.hpp"

namespace hive::automation {
std::shared_ptr<const workflows::RouteCatalog> live_hive_routes(double speed,TravelAccess access,movement::Source source,bool walking_only) {
    struct Cache {
        std::mutex mutex;
        std::shared_ptr<const movement::Recipes> recipes;
        std::shared_ptr<const workflows::RouteCatalog> catalog;
    };
    auto cache=std::make_shared<Cache>();
    auto refresh=[cache,source=std::move(source),speed,access,walking_only] {
        const auto recipes=movement::snapshot(source);
        std::lock_guard lock(cache->mutex);
        if (recipes!=cache->recipes) {
            auto catalog=walking_only?walking_hive_routes(speed,access,recipes):hive_routes(speed,access,recipes);
            cache->recipes=recipes; cache->catalog=std::move(catalog);
        }
        return cache->catalog;
    };
    auto initial=refresh();
    return std::make_shared<workflows::RouteCatalog>(*initial,std::move(refresh));
}

std::shared_ptr<const workflows::RouteCatalog> accessible_routes(const workflows::RouteCatalog& catalog,TravelAccess access) {
    if (access.bees>50) throw std::invalid_argument("Bee count must be between 0 and 50");
    const auto minimum=[](const std::string& location) {
        for (const auto& field:game::fields())
            if (location==field.location() || location==field.planting_location()) return field.minimum_bees;
        static const std::map<std::string,unsigned> gates{
            {"npc.polar-bear",15},{"npc.honey-bee",15},{"station.sticker-stack",15},
            {"station.clock",5},{"station.treats",10},{"challenge.ant",20},{"station.ant-pass",20},{"station.ant-pass-paid",20},
            {"station.memory-basic",10},{"station.memory-mega",15},{"station.memory-night",30},{"station.memory-extreme",35},
            {"station.coconut",35},{"station.glue",20},{"station.robo-pass",30},{"station.mountain-booster",25},{"station.sticker-printer",25},
            {"boss.tunnel-bear",10},{"boss.tunnel-bear.waiting",10},{"boss.commando",15},{"station.blender",15},{"station.wind-shrine",35}};
        const auto found=gates.find(location); return found==gates.end()?0u:found->second;
    };
    std::vector<workflows::ExecutableRoute> routes;
    for (const auto& route:catalog.network().routes()) {
        if (std::max(minimum(route.from),minimum(route.to))>access.bees) continue;
        const auto& path=catalog.checkpoints(route.id);
        const bool cannon=std::any_of(path.begin(),path.end(),[](const auto& p) { return p.detector=="transport.red-cannon"; });
        // The checkbox declares cannon access; bee count gates destinations.
        if (cannon && !access.cannon) continue;
        routes.push_back({route,path});
        for (const auto& station:game::stations()) if (station.location==route.to) {
            auto& scoped=routes.back();
            if (scoped.checkpoints.back().detector==route.to) {
                // The route supplies identity; the shared visual banner supplies proximity.
                scoped.checkpoints.back().detector="interaction.station";
                scoped.route.arrival=game::RouteArrival::estimated;
            }
            break;
        }
    }
    auto locations=detail::locations(routes);
    return std::make_shared<workflows::RouteCatalog>(std::move(locations),std::move(routes));
}

std::shared_ptr<const workflows::RouteCatalog> walking_hive_routes(double speed,TravelAccess access,std::shared_ptr<const movement::Recipes> recipes) {
    if (!recipes) recipes=movement::snapshot();
    const auto fields=walking_field_routes(speed,recipes);
    const auto& network=fields->network();
    std::vector<std::string> locations(network.locations().begin(),network.locations().end());
    std::vector<workflows::ExecutableRoute> routes;
    for (const auto& route : network.routes()) routes.push_back({route,fields->checkpoints(route.id)});
    const auto npcs=walking_npc_routes(speed,recipes);
    for (const auto& route : npcs->network().routes()) routes.push_back({route,npcs->checkpoints(route.id)});
    const auto planting=walking_planting_routes(speed,recipes);
    for (const auto& route : planting->network().routes()) routes.push_back({route,planting->checkpoints(route.id)});
    const auto memory=memory_routes(speed,false,recipes);
    for (const auto& route:memory->network().routes()) routes.push_back({route,memory->checkpoints(route.id)});
    const auto items=item_routes(speed,false,recipes);
    for (const auto& route:items->network().routes()) routes.push_back({route,items->checkpoints(route.id)});
    const auto bosses=boss_routes(speed,recipes);
    for (const auto& route:bosses->network().routes()) routes.push_back({route,bosses->checkpoints(route.id)});
    locations=detail::locations(routes);
    locations.insert(locations.end(),{"station.honey","station.blueberries","station.strawberries","station.clock","station.treats"});
    for (unsigned slot=1;slot<=6;++slot) {
        auto path=recipes->path("dispensers",{{"name",std::string("honey")},{"slot",int(slot)}},speed);
        path.push_back({"station.honey",{},{},Time(5000),Time(2000),0});
        const auto duration=detail::estimate(path)+Time(1500);
        routes.push_back({{"hive-"+std::to_string(slot)+"-honey","hive.proximity","station.honey",duration,
            {"hive.slot."+std::to_string(slot)},game::RouteArrival::observed,{"hive.camera.prepared"}},std::move(path)});
    }
    for (unsigned slot=1;slot<=6;++slot) {
        auto path=recipes->path("dispensers",{{"name",std::string("honey-return")},{"slot",int(slot)}},speed);
        path.push_back({"hive.proximity",{},{},Time(5000),Time(2000),0});
        const auto duration=detail::estimate(path)+Time(1500);
        routes.push_back({{"honey-hive-"+std::to_string(slot),"station.honey","hive.proximity",duration,
            {"hive.slot."+std::to_string(slot)}},std::move(path)});
    }
    for (unsigned slot=1;slot<=6;++slot) for (const std::string name : {"blueberries","strawberries","clock","treats"}) {
        auto approach=recipes->path("dispensers",{{"name",name},{"slot",int(slot)},{"cannon",false}},speed);
        approach.push_back({"station."+name,{},{},Time(5000),Time(2000),0});
        Time timeout{1500};
        for (const auto& checkpoint : approach) timeout+=checkpoint.timeout;
        const auto estimate=detail::estimate(approach)+Time(1500);
        routes.push_back({{"hive-"+std::to_string(slot)+"-"+name,"hive.proximity","station."+name,estimate,
            {"hive.slot."+std::to_string(slot)},game::RouteArrival::observed,{"hive.camera.prepared"},timeout},std::move(approach)});
    }
    return accessible_routes(workflows::RouteCatalog(std::move(locations),std::move(routes)),access);
}
std::shared_ptr<const workflows::RouteCatalog> hive_routes(double speed,TravelAccess access,std::shared_ptr<const movement::Recipes> recipes) {
    if (!recipes) recipes=movement::snapshot();
    const auto walking=walking_hive_routes(speed,{},recipes);
    std::vector<workflows::ExecutableRoute> routes;
    for (const auto& route : walking->network().routes()) routes.push_back({route,walking->checkpoints(route.id)});
    for (unsigned slot=1;slot<=6;++slot) {
        auto path=recipes->path("dispensers",{{"name",std::string("royal-jelly")},{"slot",int(slot)}},speed);
        path.push_back({"station.royal-jelly",{},{},Time(5000),Time(2000),0});
        Time timeout{1500}; for (const auto& checkpoint : path) timeout+=checkpoint.timeout;
        const auto estimate=detail::estimate(path)+Time(1500);
        routes.push_back({{"hive-"+std::to_string(slot)+"-royal-jelly","hive.proximity","station.royal-jelly",estimate,
            {"hive.slot."+std::to_string(slot)},game::RouteArrival::observed,{"hive.camera.prepared"},timeout},std::move(path)});
    }
    if (access.cannon) {
        const auto memory=memory_routes(speed,true,recipes);
        for (const auto& route:memory->network().routes()) routes.push_back({route,memory->checkpoints(route.id)});
        const auto items=item_routes(speed,true,recipes);
        for (const auto& route:items->network().routes()) routes.push_back({route,items->checkpoints(route.id)});
        const auto cannon=cannon_field_routes(speed,recipes);
        for (const auto& route:cannon->network().routes()) routes.push_back({route,cannon->checkpoints(route.id)});
        for (unsigned slot=1;slot<=6;++slot) {
            auto path=recipes->path("dispensers",{{"name",std::string("clock")},{"slot",int(slot)},{"cannon",true}},speed);
            path.push_back({"station.clock",{},{},Time(5000),Time(2000),0});
            Time timeout{1500}; for (const auto& checkpoint:path) timeout+=checkpoint.timeout;
            const auto estimate=detail::estimate(path)+Time(1500);
            routes.push_back({{"hive-"+std::to_string(slot)+"-clock-cannon","hive.proximity","station.clock",estimate,
                {"hive.slot."+std::to_string(slot)},game::RouteArrival::observed,{"hive.camera.prepared"},timeout},std::move(path)});
        }
        for (unsigned slot=1;slot<=6;++slot) for (const std::string name:{"treats","blueberries","strawberries"}) {
            auto path=recipes->path("dispensers",{{"name",name},{"slot",int(slot)},{"cannon",true}},speed);
            path.push_back({"station."+name,{},{},Time(5000),Time(2000),0});
            Time timeout{1500}; for (const auto& step:path) timeout+=step.timeout;
            const auto estimate=detail::estimate(path)+Time(1500);
            routes.push_back({{"hive-"+std::to_string(slot)+"-"+name+"-shortcut","hive.proximity","station."+name,estimate,
                {"hive.slot."+std::to_string(slot)},game::RouteArrival::observed,{"hive.camera.prepared"},timeout},std::move(path)});
        }
        const auto boosters=shortcut_booster_routes(speed,recipes);
        for (const auto& route:boosters->network().routes()) routes.push_back({route,boosters->checkpoints(route.id)});
    }
    const auto ants=ant_routes(speed,recipes);
    for (const auto& route:ants->network().routes()) routes.push_back({route,ants->checkpoints(route.id)});
    const auto boosters=walking_booster_routes(speed,recipes);
    for (const auto& route:boosters->network().routes()) routes.push_back({route,boosters->checkpoints(route.id)});
    auto locations=detail::locations(routes);
    return accessible_routes(workflows::RouteCatalog(std::move(locations),std::move(routes)),access);
}

}
