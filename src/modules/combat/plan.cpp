#include "plan.hpp"
#include "modules/shared/movement.hpp"
#include <algorithm>
#include <random>

namespace hive::features::combat {
namespace {
class Availability final : public automation::AvailabilitySource {
    std::map<std::string,std::string> enemies_;
    std::shared_ptr<const game::EnemySource> source_;
    std::set<std::string> hunts_;
public:
    Availability(std::map<std::string,std::string> enemies,std::shared_ptr<const game::EnemySource> source,std::set<std::string> hunts={})
        :hunts_(std::move(hunts)),enemies_(std::move(enemies)),source_(std::move(source)) {
        if (!source_ || enemies_.empty() || enemies_.size()>32) throw std::invalid_argument("Combat requires enemy observations and identities");
        for (const auto& [id,name] : enemies_) if (id.empty() || id.size()>80 || name.empty() || name.size()>80)
            throw std::invalid_argument("Invalid enemy identity");
    }
    Observation<bool> read(const std::string& id,const automation::View& view) const override {
        if (hunts_.contains(id)) return {Knowledge::present,true,view.context.now,view.context.window,view.context.generation};
        const auto found=enemies_.find(id);
        if (found==enemies_.end()) return {};
        const auto observed=source_->read(view.context);
        if (!observed.has_value(view.context,Time(2000))) return {};
        if (std::none_of(observed.value.begin(),observed.value.end(),[&](const auto& enemy) {
                return enemy.name==found->second && enemy.health_bar.valid();
            })) return {};
        return {Knowledge::present,true,observed.captured,observed.window,observed.generation};
    }
};
}
std::shared_ptr<const automation::AvailabilitySource> availability(std::map<std::string,std::string> enemies,
    std::shared_ptr<const game::EnemySource> source) {
    return std::make_shared<Availability>(std::move(enemies),std::move(source));
}
std::shared_ptr<const automation::AvailabilitySource> availability_for(const Plan& plan,std::shared_ptr<const game::EnemySource> source) {
    std::map<std::string,std::string> identities;
    for (const auto& enemy : plan.enemies) if (!identities.emplace(enemy.id,enemy.species).second)
        throw std::invalid_argument("Duplicate combat enemy identity");
    std::set<std::string> hunts;
    for (const auto& enemy:plan.enemies) if (!enemy.hunt_fields.empty()) hunts.insert(enemy.id);
    return std::make_shared<Availability>(std::move(identities),std::move(source),std::move(hunts));
}
std::vector<std::string> select_hunt_fields(const std::vector<std::string>& available,const SettingSelection& fields) {
    std::vector<std::string> selected; std::set<std::string> seen;
    for (const auto& field:fields) {
        if (!seen.insert(field).second) throw std::invalid_argument("Duplicate hunt field");
        if (std::find(available.begin(),available.end(),field)!=available.end()) selected.push_back(field);
    }
    if (selected.empty()) throw std::invalid_argument("Select a reachable Vicious search field");
    return selected;
}
automation::VisitPlan visit_plan(Plan plan) {
    if (plan.options.respawn_reduction<0 || plan.options.respawn_reduction>100)
        throw std::invalid_argument("Respawn reduction must be between 0 and 100 percent");
    automation::VisitPlan visits{{},std::move(plan.finish_at),plan.config,std::move(plan.setup)};
    visits.recovery=std::move(plan.recovery);
    for (const auto& [id,options]:plan.options.enemies)
        if (std::none_of(plan.enemies.begin(),plan.enemies.end(),[&](const auto& enemy) { return enemy.id==id; })
            || options.fight_limit<Time{} || options.fight_limit>std::chrono::hours(6)
            || (options.fight_limit>Time{} && options.fight_limit<std::chrono::minutes(1)))
            throw std::invalid_argument("Invalid enemy preferences");
    if (!plan.options.boss_order.empty()) std::stable_sort(plan.enemies.begin(),plan.enemies.end(),[&](const auto& a,const auto& b) {
        const auto rank=[&](const auto& enemy) { return enemy.boss?1+std::distance(plan.options.boss_order.begin(),std::find(plan.options.boss_order.begin(),plan.options.boss_order.end(),enemy.id)):0; };
        return rank(a)<rank(b);
    });
    std::map<std::string,std::function<void(automation::Visit&,const movement::Recipes&)>> refreshers;
    for (auto& enemy : plan.enemies) {
        bool mirror_loot=false;
        automation::ProgramFactory approach;
        std::optional<Time> defeat_cooldown;
        if (enemy.cooldown<Time::zero() || enemy.cooldown>std::chrono::days(7))
            throw std::invalid_argument("Invalid enemy cooldown");
        if (enemy.respawn_reduction) enemy.cooldown=Time((enemy.cooldown.count()*(100-plan.options.respawn_reduction)+99)/100);
        if (const auto options=plan.options.enemies.find(enemy.id); options!=plan.options.enemies.end()) {
            if (!options->second.collect_loot) enemy.loot.clear();
            if (options->second.baby_love) {
                if (enemy.species!="King Beetle" && enemy.species!="Tunnel Bear") throw std::invalid_argument("Baby Love preparation is only available for King Beetle and Tunnel Bear");
                if (!plan.buffs) throw std::invalid_argument("Baby Love preparation needs buff readings");
                approach=[location=enemy.destination+".waiting",name=enemy.species,source=plan.buffs,speed=plan.config.speed,movement=plan.movement] {
                    auto entry=movement::snapshot(movement)->path("bosses",{{"enemy",name},{"slot",1},{"stage",std::string("entry")}},speed);
                    return wait_for_buff(location,"Baby Love",source,speed,std::move(entry));
                };
            }
            if (options->second.fight_limit>Time{}) enemy.fight.duration=options->second.fight_limit;
            if (const auto* mondo=std::get_if<MondoOptions>(&options->second.boss)) {
                if (enemy.species!="Mondo Chick" || mondo->seconds<5 || mondo->seconds>900 || mondo->lead_seconds<0 || mondo->lead_seconds>300
                    || mondo->loot_seconds<1 || mondo->loot_seconds>120
                    || (mondo->mode!=MondoMode::kill && mondo->mode!=MondoMode::buff)
                    || (mondo->direction!=LootDirection::left && mondo->direction!=LootDirection::right && mondo->direction!=LootDirection::random))
                    throw std::invalid_argument("Invalid Mondo preferences");
                if (mondo->lead_seconds) {
                    const auto clock=plan.wall_clock; const auto lead=Time(mondo->lead_seconds*1000);
                    enemy.scheduled=[clock,lead](const ObservationContext&) { const auto phase=clock()%std::chrono::hours(1); return phase<std::chrono::minutes(15) || phase>=std::chrono::hours(1)-lead; };
                    approach=[clock,speed=plan.config.speed] {
                        const auto now=clock(),phase=now%std::chrono::hours(1);
                        return wait_for_spawn("mountaintop.entry",phase>=std::chrono::minutes(15)?now+std::chrono::hours(1)-phase:now,clock,speed);
                    };
                }
                if (mondo->mode==MondoMode::buff) {
                    enemy.session_limit=Time(mondo->seconds*1000); enemy.loot.clear();
                    enemy.fight.duration=std::max(enemy.fight.duration,enemy.session_limit+Time(15000));
                }
                else {
                    enemy.loot_duration=Time(mondo->loot_seconds*1000);
                    const bool right=mondo->direction==LootDirection::right
                        || (mondo->direction==LootDirection::random && std::random_device{}()%2);
                    mirror_loot=right;
                    if (right) for (auto& step:enemy.loot) {
                        const auto mirror=[](Key key) { return key==Key::left?Key::right:key==Key::right?Key::left:key; };
                        step.leg.first=mirror(step.leg.first); if (step.leg.second) step.leg.second=mirror(*step.leg.second);
                    }
                }
            } else if (const auto* commando=std::get_if<CommandoOptions>(&options->second.boss)) {
                if (enemy.species!="Commando Chick") throw std::invalid_argument("Commando preferences need Commando Chick");
                if (commando->health<0 || commando->health>commando_health(25)) throw std::invalid_argument("Invalid Commando starting health");
                enemy.maximum_health=commando_health(commando->level);
                enemy.initial_health=commando->health?std::min(commando->health,enemy.maximum_health):enemy.maximum_health;
                enemy.session_limit=commando->until_defeated?Time(std::chrono::hours(6)):enemy.fight.duration;
                enemy.revisit=[continuous=commando->until_defeated] { return continuous?Time(1):Time(std::chrono::minutes(30)); };
                enemy.fight.duration=enemy.session_limit+Time(15000);
            } else if (const auto* hunt=std::get_if<HuntOptions>(&options->second.boss)) {
                if (enemy.hunt_fields.empty()) throw std::invalid_argument("Hunt preferences need a hunting target");
                enemy.hunt_fields=select_hunt_fields(enemy.hunt_fields,hunt->fields);
                if (hunt->daily_only) defeat_cooldown=std::chrono::hours(22);
            } else if (const auto* beetle=std::get_if<BeetleOptions>(&options->second.boss)) {
                if (enemy.species!="King Beetle") throw std::invalid_argument("Beetle preferences need King Beetle");
                enemy.keep_old=beetle->keep_old;
            } else if (const auto* snail=std::get_if<SnailOptions>(&options->second.boss)) {
                if (enemy.species!="Stump Snail" || snail->health<0 || snail->health>enemy.maximum_health)
                    throw std::invalid_argument("Invalid Stump Snail preferences");
                enemy.initial_health=snail->health; enemy.keep_old=snail->keep_old;
                enemy.session_limit=snail->until_defeated?std::chrono::hours(6):enemy.fight.duration;
                enemy.revisit=[continuous=snail->until_defeated] { return continuous?Time(1):Time(std::chrono::minutes(30)); };
                enemy.fight.duration=enemy.session_limit+Time(15000);
            }
        }
        if (!enemy.progress && (enemy.session_limit>Time{} || enemy.maximum_health)) enemy.progress=std::make_shared<FightProgress>();
        if (plan.movement && enemy.hunt_fields.empty()) refreshers.emplace(enemy.id,[enemy,mirror_loot,speed=plan.config.speed](automation::Visit& visit,const movement::Recipes& recipes) {
            auto updated=enemy;
            updated.fight.legs=movement_walks(recipes,"combat",{{"enemy",enemy.species},{"loot",false}},true);
            if (!enemy.loot.empty()) {
                updated.loot=movement_walks(recipes,"combat",{{"enemy",enemy.species},{"loot",true}});
                if (mirror_loot) for (auto& walk:updated.loot) {
                    const auto mirror=[](Key key) { return key==Key::left?Key::right:key==Key::right?Key::left:key; };
                    walk.leg.first=mirror(walk.leg.first); if (walk.leg.second) walk.leg.second=mirror(*walk.leg.second);
                }
            }
            visit.action=updated.fight; visit.loot=updated.loot;
            if (updated.observations) visit.action_program=[updated,speed](Time entered) { return encounter(updated,speed,entered); };
        });
        const auto progress=enemy.progress;
        const auto observed=enemy.observations?std::function<std::unique_ptr<automation::Program>(Time)>{[enemy,speed=plan.config.speed,prepare=visits.setup,home=visits.recovery,movement=plan.movement](Time entered) -> std::unique_ptr<automation::Program> {
            if (!enemy.hunt_fields.empty()) return hunt(enemy,speed,prepare,home,movement);
            return encounter(enemy,speed,entered);
        }}:std::function<std::unique_ptr<automation::Program>(Time)>{};
        visits.visits.push_back({std::move(enemy.id),std::move(enemy.destination),std::move(enemy.fight),std::move(enemy.loot),defeat_cooldown.value_or(enemy.cooldown),enemy.inspect_when_unknown,plan.options.reset_return && visits.recovery?visits.recovery:std::move(enemy.departure),std::move(enemy.approach_scope),std::move(enemy.approach_location),std::move(enemy.group)});
        visits.visits.back().action_program=observed;
        if (enemy.observations && enemy.hunt_fields.empty())
            visits.visits.back().ready_to_depart=[source=game::enemy_source(enemy.observations)](const automation::View& view) {
                return source->read(view.context).has_value(view.context,Time(2000));
            };
        visits.visits.back().approach=std::move(approach);
        visits.visits.back().working_status="Fighting";
        if (!visits.visits.back().loot.empty()) visits.visits.back().settle=Time(500);
        visits.visits.back().scheduled=std::move(enemy.scheduled);
        visits.visits.back().loot_duration=enemy.loot_duration;
        if (progress) visits.visits.back().retry_after_completion=[progress] { return progress->read().retry; };
    }
    if (plan.movement) visits.prepare=[source=plan.movement,refreshers=std::move(refreshers)](std::vector<automation::Visit>& selected) {
        const auto recipes=movement::snapshot(source);
        for (auto& visit:selected) if (const auto found=refreshers.find(visit.id);found!=refreshers.end()) found->second(visit,*recipes);
    };
    return visits;
}
automation::VisitRun program(Plan plan,std::shared_ptr<const game::RouteNetwork> network,
    std::shared_ptr<const automation::AvailabilitySource> availability) {
    return automation::visit_program(visit_plan(std::move(plan)),std::move(network),std::move(availability));
}
}
