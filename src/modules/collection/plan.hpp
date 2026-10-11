#pragma once
#include "game/actions/activities/visits.hpp"
#include "game/detection/detection.hpp"
#include "items.hpp"
#include "game/data/stations.hpp"

namespace hive::workflows { class RouteCatalog; }

namespace hive::features::collection {
struct Station {
    std::string id,destination;
    automation::UseStation interaction;
    Time cooldown{};
    bool inspect_when_unknown{};
    automation::ProgramFactory departure;
    bool selected_by_default{true};
    Time settle{};
    automation::ProgramFactory action;
    std::function<bool(const ObservationContext&)> scheduled;
    std::function<std::optional<Time>()> retry_after_completion;
    game::StationInteraction kind{game::StationInteraction::activate};
    std::function<Time()> cooldown_after_completion;
};
struct Plan {
    std::vector<Station> stations;
    std::optional<std::string> finish_at;
    automation::RunConfig config{Time(3600000),28,{},true,true};
    automation::ProgramFactory setup;
    automation::ProgramFactory recovery;
    std::shared_ptr<const game::DetectionSource> item_menus;
    ItemOptions items;
    std::shared_ptr<const game::InventorySource> inventory;
};
void configure_items(Plan& plan);
Plan catalog_plan();
Plan default_plan(std::shared_ptr<const game::DetectionSource> memory={},std::shared_ptr<const game::DetectionSource> daylight={},
    std::shared_ptr<const game::DetectionSource> item_menus={});
Plan routed_plan(const workflows::RouteCatalog& routes,double speed,automation::ProgramFactory return_to_hive={},
    std::shared_ptr<const game::DetectionSource> memory={},std::shared_ptr<const game::DetectionSource> daylight={},
    std::shared_ptr<const game::DetectionSource> item_menus={});
automation::VisitPlan visit_plan(Plan plan);
std::shared_ptr<const automation::AvailabilitySource> availability(const Plan& plan);
automation::VisitRun program(Plan plan,std::shared_ptr<const game::RouteNetwork> network,
    std::shared_ptr<const automation::AvailabilitySource> availability);
}
