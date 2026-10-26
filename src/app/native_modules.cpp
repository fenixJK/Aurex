#include "native_modules.hpp"
#include "modules/tools/module.hpp"
#include "modules/combat/vichop/module.hpp"
#include "game/actions/recovery/reconnect.hpp"
#include "modules/quests/actions.hpp"
#include "core/module_host/host.hpp"
#include "modules/shared/policies.hpp"
#include "game/actions/travel/field_routes.hpp"
#include "game/actions/travel/hive_routes.hpp"
#include "game/actions/travel/travel_preparation.hpp"
#include "game/data/fields.hpp"
#include "game/data/stations.hpp"
#include "game/data/npcs.hpp"
#include "modules/planters/actions.hpp"
#include <algorithm>

namespace hive::app {
namespace {
class Requirements {
    std::string missing_;
public:
    void need(bool ready,const char* name) {
        if (ready) return;
        if (!missing_.empty()) missing_+=", ";
        missing_+=name;
    }
    void navigation(const automation::NavigationServices& services) {
        need(bool(services.routes),"calibrated routes");
        need(bool(services.perception),"location detection");
    }
    void destinations(const automation::NavigationServices& services,const std::vector<std::string>& locations) {
        if (!services.routes) return;
        need(std::all_of(locations.begin(),locations.end(),[&](const auto& location) {
            return services.routes->network().contains(location);
        }),"routes for the configured destinations");
    }
    std::string message() const { return missing_.empty()?"":"Not ready: "+missing_+" still need to be connected."; }
};
void add(extensions::Host& host,extensions::Manifest manifest,extensions::Factory factory,const Requirements& requirements,
    extensions::Host::SettingsMigration migration={}) {
    game::RequestGrants grants;
    for (const auto& channel : manifest.requests) grants.emplace(channel,game::RequestLimits{});
    host.register_provider(manifest.provider,[id=manifest.id,factory=std::move(factory)](const extensions::Manifest& requested) {
        if (requested.id!=id) throw std::invalid_argument("Compiled provider belongs to a different module");
        return factory(requested);
    },std::move(grants));
    host.register_module(std::move(manifest),requirements.message(),std::move(migration));
}
}
void configure_travel_modules(NativeModules& modules,std::optional<double> speed,const HiveSources& hive,
    std::shared_ptr<const game::DetectionSource> enemies) {
    modules.collection.configuration=features::collection::catalog_plan();
    modules.combat.configuration=features::combat::catalog_plan();
    if (!speed) {
        modules.startup={}; modules.navigation.routes.reset(); modules.quests.cycle.reset();
        modules.planters.plan={};
        modules.collection.plan={}; modules.collection.availability.reset();
        modules.combat.plan={}; modules.combat.availability.reset();
        modules.combat.arrival={}; modules.combat.joined_setup={};
        if (modules.planters.inventory) modules.planters.actions.reset();
        auto routes=automation::live_hive_routes(28,modules.travel_access,modules.movement,true);
        modules.gathering=features::gathering::field_services(routes->network(),28,modules.patterns,modules.screen_reference);
        if(modules.gathering.sprinklers) modules.gathering.sprinklers->equipment=modules.sprinklers;
        return;
    }
    if (!hive.slot_source || !hive.spawn || !hive.reference || !hive.proximity || !hive.result || !modules.navigation.perception)
        throw std::invalid_argument("Walking routes need hive detection and navigation");
    auto routes=automation::live_hive_routes(*speed,modules.travel_access,modules.movement,!hive.recovery);
    const automation::FieldReturnOptions returns{hive.result,modules.travel_access,modules.transport_prompt,modules.movement};
    auto service=features::gathering::field_services(routes->network(),*speed,modules.patterns,modules.screen_reference,returns);
    if(service.sprinklers) service.sprinklers->equipment=modules.sprinklers;
    const auto setup=[movement=modules.movement_setup,hive,speed=*speed](automation::HiveStart start) -> automation::ProgramFactory {
        return [movement,hive,speed,start] {
            return automation::prepare_travel(movement,[hive,speed,start] {
                return automation::acquire_hive(hive.slot_source,hive.spawn,hive.reference,hive.proximity,hive.options(speed),start);
            },speed);
        };
    };
    service.plan.hive_setup=setup(automation::HiveStart::locate);
    modules.combat.arrival=hive.recovery?automation::ProgramFactory([source=hive.recovery] { return automation::wait_for_reconnect(source); }):automation::ProgramFactory{};
    modules.combat.joined_setup=[movement=modules.movement_setup,hive,speed=*speed] {
        return automation::prepare_travel(movement,[hive,speed] {
            auto options=hive.options(speed); options.existing_slot=0; options.confirmed_spawn=true;
            return automation::acquire_hive(hive.slot_source,hive.spawn,hive.reference,hive.proximity,options);
        },speed);
    };
    modules.startup=setup(hive.recovery?automation::HiveStart::reset:automation::HiveStart::locate);
    if (hive.recovery) service.plan.recovery=[hive,speed=*speed] {
        return automation::recover_to_hive(hive.slot_source,hive.spawn,hive.reference,hive.proximity,
            hive.options(speed));
    };
    automation::ProgramFactory return_to_hive;
    if (hive.recovery) return_to_hive=[hive,speed=*speed] {
        return automation::respawn_to_hive(hive.slot_source,hive.spawn,hive.reference,hive.proximity,
            hive.options(speed));
    };
    service.plan.route_recovery=return_to_hive;
    if (return_to_hive) service.plan.whirligig_return=[hive,cooldown=modules.whirligig](unsigned slot,automation::ProgramFactory return_method) {
        return automation::whirligig_to_hive(slot,cooldown,hive.proximity,[hive] {
            return automation::prepare_hive_view(hive.reference,hive.proximity,hive.result);
        },std::move(return_method));
    };
    if (modules.quests.conversation && service.plan.hive_setup && return_to_hive)
        modules.quests.cycle=features::quests::CyclePlan{{},service.plan.hive_setup,return_to_hive,modules.quests.conversation,*speed,modules.quests.cooldowns};
    else modules.quests.cycle.reset();
    auto planters=features::planters::walking_plan(*routes,*speed,return_to_hive,returns);
    planters.setup=service.plan.hive_setup;
    planters.death_recovery=service.plan.recovery;
    planters.conversion=features::planters::HarvestConversion{95,true,true};
    if (modules.planters.inventory && modules.planters.harvest_prompt) {
        planters.attempt_unscanned_placement=true;
        modules.planters.actions=planters.destinations.empty()?nullptr:
            features::planters::desktop_actions(planters,modules.planters.inventory,modules.planters.harvest_prompt);
    }
    modules.collection.plan=features::collection::routed_plan(*routes,*speed,return_to_hive,modules.collection.memory,modules.collection.daylight,modules.collection.item_menus);
    modules.collection.plan.inventory=modules.collection.inventory;
    modules.collection.plan.items.stock=modules.stock; modules.collection.plan.items.inventory=modules.collection.inventory;
    modules.collection.plan.items.orders=modules.collection.item_orders;
    modules.collection.plan.items.ant_passes=modules.collection.ant_passes;
    modules.collection.plan.items.movement=modules.movement;
    features::collection::configure_items(modules.collection.plan);
    modules.collection.plan.setup=service.plan.hive_setup;
    modules.collection.plan.recovery=return_to_hive;
    modules.collection.availability=features::collection::availability(modules.collection.plan);
    if (modules.quests.cycle) features::quests::configure_actions(*modules.quests.cycle,routes->network(),
        modules.quests.inventory,modules.quests.hive_view,hive.reference,modules.blooms,modules.movement,modules.collection.cooldowns,
        [plan=modules.collection.plan,network=std::make_shared<game::RouteNetwork>(routes->network()),
            source=modules.collection.availability,saved=modules.collection.cooldowns](const std::string& id) {
            if (id=="Ant Pass Dispenser") return network->contains("station.ant-pass-paid")?features::collection::ant_pass_purchase(plan.items.ant_passes,plan.setup,plan.recovery,plan.inventory,plan.config.speed):automation::ProgramFactory{};
            return features::visits::station_request(features::collection::visit_plan(plan),id,network,source,saved);
        },modules.quests.browser);
    modules.combat.plan={}; modules.combat.availability.reset();
    if (enemies) {
        modules.combat.plan=features::combat::walking_plan(*routes,*speed,enemies,return_to_hive,returns,modules.collection.daylight);
        for (auto& enemy:modules.combat.plan.enemies) if (enemy.species=="Vicious Bee" && modules.combat.vicious) {
            enemy.vicious=modules.combat.vicious;
            // The fast search follows the established field order without waking OCR.
            enemy.requested_field={};
        }
        modules.combat.plan.setup=service.plan.hive_setup;
        modules.combat.plan.recovery=return_to_hive;
        modules.combat.plan.buffs=modules.buffs;
        if (!modules.combat.plan.enemies.empty()) modules.combat.availability=features::combat::availability_for(modules.combat.plan,game::enemy_source(enemies));
    }
    modules.navigation.routes=std::move(routes); modules.gathering=std::move(service);
    modules.planters.plan=std::move(planters);
}
void register_native_modules(extensions::Host& host,const NativeModules& modules) {
    const auto navigation=modules.navigation;
    if(modules.accounts) add(host,features::accounts::module_manifest(),features::accounts::module_factory(modules.accounts),Requirements{});
    add(host,features::tools::module_manifest(),features::tools::module_factory(modules.collection.inventory),Requirements{},
        [](int,Settings settings) {
            settings.erase("ssa-delay");
            settings.erase("jelly-delay");
            settings.erase("bitter-delay");
            if (const auto limit=settings.find("ssa-limit"); limit!=settings.end())
                if (auto* value=std::get_if<int>(&limit->second)) *value=std::max(10,*value);
            return settings;
        });
    if (modules.field_boosts) {
        std::vector<features::boosting::Booster> boosters;
        for (const auto& station:modules.collection.plan.stations) {
            if (!navigation.routes || !modules.collection.availability) break;
            const auto& catalog=game::stations();
            const auto definition=std::find_if(catalog.begin(),catalog.end(),[&](const auto& item) { return item.name==station.id; });
            if (definition==catalog.end() || definition->boosted_fields.empty() || station.id=="Coconut Dispenser") continue;
            auto visit=features::visits::station_request(features::collection::visit_plan(modules.collection.plan),station.id,
                std::make_shared<game::RouteNetwork>(navigation.routes->network()),modules.collection.availability,modules.collection.cooldowns);
            boosters.push_back({station.id,std::move(visit),[saved=modules.collection.cooldowns,id=station.id,cooldown=station.cooldown] {
                if (saved) saved->load();
                return saved?saved->remaining(id,cooldown):Time{};
            },definition->boosted_fields});
        }
        std::vector<std::string> fields;
        for (const auto& field:modules.gathering.plan.fields) {
            const auto definition=game::field_at(field.preference.location);
            if (!definition || definition->minimum_bees<=modules.travel_access.bees) fields.push_back(field.preference.field);
        }
        Requirements requirements; requirements.navigation(navigation);
        auto manifest=features::boosting::module_manifest();
        modules.boosting->scheduling(host.work_schedule(),manifest.id);
        modules.boosting->supplies(modules.stock);
        add(host,std::move(manifest),features::boosting::module_factory(modules.boosting,modules.field_boosts,boosters,fields,modules.stock,modules.collection.inventory),requirements,features::boosting::migrate_settings);
    }
    {
        auto service=modules.gathering;
        service.plan.buffs=modules.buffs;
        service.plan.field_boosts=modules.field_boosts;
        if (!service.policy) service.policy=std::make_shared<policies::DefaultFieldPolicy>();
        Requirements requirements; requirements.navigation(navigation);
        requirements.need(!service.plan.fields.empty() && !service.plan.hive.empty(),"field and hive setup");
        requirements.need(!modules.patterns.empty(),"valid Lua gathering patterns");
        std::vector<std::string> destinations{service.plan.hive};
        for (const auto& field : service.plan.fields) destinations.push_back(field.preference.location);
        requirements.destinations(navigation,destinations);
        auto manifest=features::gathering::module_manifest(service.plan,"hive.native-gathering","native-gathering",service.controls,service.sprinklers);
        manifest.name="Gathering";
        if (!service.description.empty()) manifest.description=service.description;
        add(host,std::move(manifest),[service,navigation,goals=host.field_goal_source()](const auto& manifest) {
            return features::gathering::module_factory(service.plan,navigation,service.policy,goals,service.controls,service.sprinklers)(manifest);
        },requirements,[equipment=service.sprinklers && service.sprinklers->equipment](int version,const Settings& saved) {
            auto values=features::gathering::migrate_settings(version,saved);
            if(equipment) for(const auto* key:{"sprinkler-type","sprinkler-slot"}) values.erase(key);
            return values;
        });
    }
    {
        auto service=modules.quests;
        if (service.cycle) service.cycle->work=host.work_schedule();
        const bool automatic=bool(service.browser) && bool(service.conversation);
        Requirements requirements;
        requirements.need(!automatic || service.cycle.has_value(),"quest travel and base movement speed");
        if (automatic) {
            requirements.navigation(navigation);
            std::vector<std::string> destinations;
            for (const auto& npc:game::npcs()) destinations.push_back(npc.location());
            requirements.destinations(navigation,destinations);
        }
        requirements.need(!service.fields.empty(),"quest field catalog");
        requirements.need(bool(service.observations),"quest card detection");
        auto manifest=features::quests::module_manifest("hive.native-quests","native-quests",bool(service.browser),automatic);
        manifest.description=service.cycle?"Scan selected quests, request gathering, combat and station visits, visit givers and confirm the next quest. Unsupported objectives remain visible for manual completion.":service.browser?"Open and scan the quest log. Supported objectives request gathering, enemy kills and station visits during a session.":"Read visible quest cards and request gathering, enemy kills and station visits during a session.";
        add(host,std::move(manifest),[service](const auto& manifest) {
            return features::quests::module_factory(service.observations,service.fields,Time(30000),40,service.browser,std::chrono::minutes(10),service.cycle)(manifest);
        },requirements,[](int version,const Settings& previous) {
            if(version==2 || version==3) return previous;
            if (version!=1 || !previous.empty()) throw std::invalid_argument("Unknown previous Quest settings version");
            return Settings{{"givers",SettingSelection{"Polar Bear"}}};
        });
    }
    {
        const auto service=modules.planters;
        Requirements requirements; requirements.navigation(navigation);
        requirements.need(!service.plan.destinations.empty(),"planter field setup");
        std::vector<std::string> destinations;
        for (const auto& [field,location] : service.plan.destinations) destinations.push_back(location);
        if (service.plan.return_to) destinations.push_back(*service.plan.return_to);
        requirements.destinations(navigation,destinations);
        requirements.need(bool(service.account),"saved planter state");
        requirements.need(bool(service.observations),"nectar and planter detection");
        requirements.need(bool(service.actions),"placement and harvest actions");
        requirements.need(bool(service.clock),"account clock");
        auto manifest=features::planters::module_manifest(service.plan,"hive.native-planters");
        manifest.description="Maintain nectar or grow material bonuses. Select fields you can enter and planters you own. Choose Walk to keep carried pollen or Reset for a respawn return. Start with no planters placed by another macro; this profile tracks its own placements. Interrupted actions are checked before another attempt.";
        add(host,std::move(manifest),[service,navigation](const auto& manifest) {
            return features::planters::module_factory(service.plan,navigation,service.account,service.observations,service.actions,service.clock,service.context)(manifest);
        },requirements,features::planters::migrate_settings);
    }
    {
        auto service=modules.collection;
        const auto booster=[](const auto& station) { return station.id.ends_with("Field Booster"); };
        std::erase_if(service.plan.stations,booster);
        if (service.configuration) std::erase_if(service.configuration->stations,booster);
        Requirements requirements; requirements.navigation(navigation);
        requirements.need(!service.plan.stations.empty(),"station setup");
        std::vector<std::string> destinations;
        for (const auto& station : service.plan.stations) destinations.push_back(station.destination);
        if (service.plan.finish_at) destinations.push_back(*service.plan.finish_at);
        requirements.destinations(navigation,destinations);
        requirements.need(bool(service.availability),"station availability detection");
        auto manifest=features::collection::module_manifest(service.configuration?*service.configuration:service.plan,"hive.native-collection","native-collection",true);
        manifest.settings_version=9;
        manifest.name="Collection";
        manifest.description="Collect dispensers, play Memory Match, craft at the Blender and donate to the Wind Shrine. Activities spend only the items and amounts you select.";
        add(host,std::move(manifest),[service,navigation,requests=host.request_source()](const auto& manifest) {
            return features::collection::module_factory(service.plan,navigation,service.availability,service.cooldowns,requests,service.configuration)(manifest);
        },requirements,[](int version,const Settings& values) {
            if (version<2 || version>8) throw std::invalid_argument("Unknown previous Collection settings version");
            auto result=version==2?features::visits::migrate_selections(values,{"Honey Dispenser"}):values;
            result.erase("follow-requests");
            for (const auto* name:{"Red Field Booster","Blue Field Booster","Mountain Field Booster"})
                result.erase(features::visits::selection_key(name));
            return features::collection::migrate_item_settings(std::move(result));
        });
    }
    {
        const auto service=modules.combat;
        Requirements requirements; requirements.navigation(navigation);
        requirements.need(!service.plan.enemies.empty(),"enemy setup");
        std::vector<std::string> destinations;
        for (const auto& enemy : service.plan.enemies) destinations.push_back(enemy.destination);
        if (service.plan.finish_at) destinations.push_back(*service.plan.finish_at);
        requirements.destinations(navigation,destinations);
        requirements.need(bool(service.availability),"enemy availability detection");
        auto manifest=features::combat::module_manifest(service.configuration?*service.configuration:service.plan,"hive.native-combat","native-combat",true);
        manifest.name="Combat";
        manifest.settings_version=6;
        manifest.description="Fight selected enemies and collect their loot when due. Enemies in the same field share a trip. Quest requests can add needed fights; Gather interrupt allows them during gathering.";
        add(host,std::move(manifest),[service,navigation,goals=host.enemy_goal_source()](const auto& manifest) {
            return features::combat::module_factory(service.plan,navigation,service.availability,goals,service.cooldowns,service.configuration)(manifest);
        },requirements,[plan=service.configuration?*service.configuration:service.plan](int version,const Settings& values) {
            return features::combat::migrate_settings(version,values,plan);
        });
    }
    {
        const auto& plan=modules.combat.plan;
        const auto enemy=std::find_if(plan.enemies.begin(),plan.enemies.end(),[](const auto& candidate) { return candidate.species=="Vicious Bee"; });
        Requirements requirements; requirements.navigation(navigation);
        requirements.need(enemy!=plan.enemies.end(),"Vicious Bee search routes");
        requirements.need(bool(plan.setup) && bool(plan.recovery),"hive setup and recovery");
        requirements.need(bool(modules.collection.daylight),"night detection");
        requirements.need(bool(modules.combat.arrival) && bool(modules.combat.joined_setup),"server arrival and hive setup");
        features::vichop::Services services;
        if (enemy!=plan.enemies.end()) services.enemy=*enemy;
        services.speed=plan.config.speed; services.prepare=plan.setup; services.home=plan.recovery;
        services.movement=plan.movement; services.daylight=modules.collection.daylight;
        services.servers=features::vichop::public_servers();
        services.arrival=modules.combat.arrival; services.joined_setup=modules.combat.joined_setup;
        add(host,features::vichop::module_manifest(plan.enemies.empty() && modules.combat.configuration?*modules.combat.configuration:plan),
            features::vichop::module_factory(std::move(services)),requirements);
    }
}
}
