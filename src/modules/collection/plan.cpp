#include "plan.hpp"
#include "games.hpp"
#include "game/data/stations.hpp"
#include "game/detection/daylight.hpp"
#include "game/actions/travel/navigation.hpp"
#include <algorithm>

namespace hive::features::collection {
void configure_items(Plan& plan) {
    for (auto& station:plan.stations) {
        if (station.id=="Glue Dispenser") {
            if (plan.inventory) station.action=[inventory=plan.inventory,speed=plan.config.speed,movement=plan.items.movement] { return collect_glue(inventory,speed,movement); };
            continue;
        }
        if (!plan.item_menus) continue;
        if (station.id=="Sticker Printer") {
            station.action=[source=plan.item_menus,egg=plan.items.printer_egg] { return print_sticker(source,egg); };
            continue;
        }
        if (station.id!="Blender" && station.id!="Wind Shrine") continue;
        auto result=std::make_shared<ItemResult>();
        station.retry_after_completion=[result] { return result->retry; };
        const auto source=plan.item_menus; const auto options=plan.items;
        if (options.orders) {
            const auto kind=station.id=="Blender"?game::ItemMenuKind::blender:game::ItemMenuKind::wind_shrine;
            station.scheduled=[orders=options.orders,kind](const ObservationContext&) { return orders->pending(kind); };
        }
        if (station.id=="Blender") {
            station.cooldown=options.orders?Time{}:std::chrono::minutes(5)*options.batches;
            station.action=[source,options,result] { return blend(source,options,result); };
        } else {
            station.cooldown=options.orders?Time{}:options.shrine_interval;
            station.action=[source,options,result,speed=plan.config.speed] { return donate(source,options,result,speed); };
        }
    }
}
Plan catalog_plan() {
    Plan plan;
    for (const auto& station:game::stations()) {
        plan.stations.push_back({station.name,station.location,{station.name},station.cooldown,true,{},station.selected_by_default,station.settle});
        plan.stations.back().kind=station.interaction;
    }
    return plan;
}
Plan default_plan(std::shared_ptr<const game::DetectionSource> memory,std::shared_ptr<const game::DetectionSource> daylight,std::shared_ptr<const game::DetectionSource> item_menus) {
    auto plan=catalog_plan();
    plan.item_menus=std::move(item_menus);
    std::erase_if(plan.stations,[&](const auto& station) {
        return station.kind==game::StationInteraction::memory_match?!memory:
            station.kind!=game::StationInteraction::activate && !plan.item_menus;
    });
    for (auto& station:plan.stations) {
        if (station.kind==game::StationInteraction::memory_match)
            station.action=[memory,name=station.id] { return memory_match(memory,name); };
        const auto& definitions=game::stations();
        const auto definition=std::find_if(definitions.begin(),definitions.end(),[&](const auto& item) { return item.name==station.id; });
        if (definition->night_only) station.scheduled=game::during_night(daylight);
    }
    configure_items(plan);
    return plan;
}
Plan routed_plan(const workflows::RouteCatalog& routes,double speed,automation::ProgramFactory return_to_hive,std::shared_ptr<const game::DetectionSource> memory,std::shared_ptr<const game::DetectionSource> daylight,std::shared_ptr<const game::DetectionSource> item_menus) {
    if (!std::isfinite(speed) || speed<8 || speed>200) throw std::invalid_argument("Collection routes need a valid movement speed");
    auto plan=default_plan(std::move(memory),std::move(daylight),std::move(item_menus)); plan.config.speed=speed; plan.finish_at="hive.proximity";
    std::set<std::string> outbound,inbound;
    for (const auto& route : routes.network().routes()) {
        if (route.from==*plan.finish_at) outbound.insert(route.to);
        if (route.to==*plan.finish_at) inbound.insert(route.from);
    }
    std::erase_if(plan.stations,[&](const auto& station) {
        return !outbound.contains(station.destination) || (!inbound.contains(station.destination) && !return_to_hive);
    });
    for (auto& station : plan.stations) if (!inbound.contains(station.destination)) {
        station.departure=return_to_hive; station.selected_by_default=false;
    }
    configure_items(plan);
    return plan;
}
namespace {
class CollectionAvailability final : public automation::AvailabilitySource {
    std::shared_ptr<const automation::AvailabilitySource> source_;
    bool glue_;
public:
    CollectionAvailability(std::shared_ptr<const automation::AvailabilitySource> source,bool glue):source_(std::move(source)),glue_(glue) {}
    Observation<bool> read(const std::string& id,const automation::View& view) const override {
        if (glue_ && id=="Glue Dispenser" && view.route_position && view.route_position->usable(view.context)
            && view.route_position->location=="station.glue")
            return {Knowledge::present,true,view.context.now,view.context.window,view.context.generation};
        if (id=="Free Ant Pass Dispenser" && view.observations) {
            const auto full=game::read_prompt_landmark(view.context,view.observations->text,
                {{"You already have 10 Ant Passes","You already have 10/10 Ant Passes","10/10 Ant Passes"},{.2,0,.6,.3},.7f,game::PromptMatch::phrase});
            if (full.has_value(view.context,Time(2000)))
                return {Knowledge::present,false,full.captured,full.window,full.generation};
        }
        return source_->read(id,view);
    }
    Observation<Time> remaining(const std::string& id,const automation::View& view) const override { return source_->remaining(id,view); }
};
}
std::shared_ptr<const automation::AvailabilitySource> availability(const Plan& plan) {
    std::map<std::string,std::string> names;
    for (const auto& station : plan.stations)
        if (!names.emplace(station.id,station.interaction.name).second) throw std::invalid_argument("Duplicate station identity");
    return std::make_shared<CollectionAvailability>(automation::station_availability(std::move(names)),bool(plan.inventory));
}
automation::VisitPlan visit_plan(Plan plan) {
    automation::VisitPlan visits{{},std::move(plan.finish_at),plan.config,std::move(plan.setup)};
    visits.recovery=std::move(plan.recovery);
    for (auto& station : plan.stations) {
        automation::Visit visit{std::move(station.id),std::move(station.destination),std::move(station.interaction),{},station.cooldown,station.inspect_when_unknown,std::move(station.departure)};
        if (station.action) visit.action_program=[action=std::move(station.action)](Time) { return action(); };
        visit.scheduled=std::move(station.scheduled);
        visit.retry_after_completion=std::move(station.retry_after_completion);
        visit.settle=station.settle; visits.visits.push_back(std::move(visit));
    }
    return visits;
}
automation::VisitRun program(Plan plan,std::shared_ptr<const game::RouteNetwork> network,
    std::shared_ptr<const automation::AvailabilitySource> availability) {
    return automation::visit_program(visit_plan(std::move(plan)),std::move(network),std::move(availability));
}
}
