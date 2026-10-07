#pragma once
#include "hive/modules/gathering/setup.hpp"
#include "hive/modules/quests/module.hpp"
#include "hive/modules/planters/module.hpp"
#include "hive/modules/collection/module.hpp"
#include "hive/modules/combat/module.hpp"
#include "hive/modules/boosting/module.hpp"
#include "hive/modules/accounts/module.hpp"
#include "hive/automation/hive_setup.hpp"
#include "hive/automation/hive_routes.hpp"
#include "hive/game/detection/screen_reference.hpp"

namespace hive::extensions { class Host; }
namespace hive::app {
using GatheringServices = features::gathering::Services;
struct QuestServices {
    std::vector<features::quests::Field> fields;
    std::shared_ptr<const features::quests::Source> observations;
    std::shared_ptr<const features::quests::BrowserSource> browser;
    std::shared_ptr<const game::ConversationSource> conversation;
    std::optional<features::quests::CyclePlan> cycle;
    std::shared_ptr<game::Cooldowns> cooldowns;
    std::shared_ptr<const game::InventorySource> inventory;
    std::shared_ptr<const game::DetectionSource> hive_view;
};
struct PlanterServices {
    features::planters::ExecutionPlan plan;
    std::shared_ptr<features::planters::Account> account;
    std::shared_ptr<const features::planters::Source> observations;
    std::shared_ptr<const features::planters::Actions> actions;
    features::planters::WallClock clock;
    std::shared_ptr<const game::InventorySource> inventory;
    std::shared_ptr<const game::PlanterSource> detection;
};
struct CollectionServices {
    features::collection::Plan plan;
    std::shared_ptr<const automation::AvailabilitySource> availability;
    std::shared_ptr<features::visits::Cooldowns> cooldowns;
    std::shared_ptr<const game::DetectionSource> memory;
    std::shared_ptr<const game::DetectionSource> daylight;
    std::shared_ptr<const game::DetectionSource> item_menus;
    std::shared_ptr<features::collection::ItemOrders> item_orders;
    std::shared_ptr<const game::InventorySource> inventory;
};
struct CombatServices {
    features::combat::Plan plan;
    std::shared_ptr<const automation::AvailabilitySource> availability;
    std::shared_ptr<features::visits::Cooldowns> cooldowns;
};
struct NativeModules {
    automation::TravelAccess travel_access;
    movement::Source movement;
    game::SprinklerPreference sprinklers;
    automation::NavigationServices navigation;
    GatheringServices gathering;
    std::shared_ptr<features::accounts::Controller> accounts;
    QuestServices quests;
    PlanterServices planters;
    CollectionServices collection;
    CombatServices combat;
    std::shared_ptr<features::boosting::Controller> boosting{std::make_shared<features::boosting::Controller>()};
    std::shared_ptr<const game::DetectionSource> field_boosts;
    std::vector<features::gathering::PatternPreset> patterns{features::gathering::default_pattern_controls().presets};
    std::shared_ptr<const game::DetectionSource> blooms;
    std::shared_ptr<const game::ScreenReferenceSource> screen_reference;
    std::shared_ptr<const game::LandmarkSource> transport_prompt;
    automation::ProgramFactory movement_setup;
    automation::ProgramFactory startup;
};
struct HiveSources {
    std::shared_ptr<const game::HiveSlotsSource> slot_source;
    std::shared_ptr<const game::LandmarkSource> spawn,reference,proximity;
    std::shared_ptr<automation::HiveSetupResult> result;
    std::shared_ptr<const game::RespawnSource> recovery;
    std::function<unsigned()> existing_slot;
    movement::Source movement;
    automation::HiveClaimOptions options(double speed) const {
        const auto slot=existing_slot?existing_slot():0;
        return {speed,slot,true,result,false,recovery,slot,movement};
    }
};
void configure_travel_modules(NativeModules& modules,std::optional<double> speed,const HiveSources& hive,
    std::shared_ptr<const game::DetectionSource> enemies={});
void register_native_modules(extensions::Host& host,const NativeModules& modules);
}
