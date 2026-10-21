#include "module.hpp"
#include "server_visit.hpp"
#include <algorithm>
#include <cmath>
#include "game/data/fields.hpp"

namespace hive::features::combat {
namespace {
const automation::Permissions permissions{automation::Permission::movement,automation::Permission::interact,automation::Permission::tool};
std::string option_key(std::string_view prefix,const std::string& id) {
    return std::string(prefix)+features::visits::selection_key(id).substr(6);
}
std::vector<extensions::Setting> option_settings(const Plan& plan) {
    std::vector<extensions::Setting> result{{"respawn-reduction","Respawn reduction (%)",plan.options.respawn_reduction,0,100,{}}};
    extensions::Setting bosses{"bosses","Boss",SettingSelection{}}; bosses.ordered=true;
    for (const auto& enemy:plan.enemies) if (enemy.boss) {
        bosses.choices.push_back(enemy.id);
        if (enemy.selected_by_default) std::get<SettingSelection>(bosses.initial).push_back(enemy.id);
    }
    if (!bosses.choices.empty()) result.push_back(std::move(bosses));
    for (const auto& enemy:plan.enemies) {
        const auto found=plan.options.enemies.find(enemy.id);
        const auto options=found==plan.options.enemies.end()?EnemyOptions{}:found->second;
        result.push_back({option_key("loot",enemy.id),"Loot",options.collect_loot});
        if (!enemy.hunt_fields.empty()) result.back().hidden=true;
        if (enemy.boss) {
            const auto duration=options.fight_limit>Time{}?options.fight_limit:enemy.fight.duration;
            extensions::Setting limit{option_key("fight-minutes",enemy.id),"Fight limit",int((duration.count()+59999)/60000),1,enemy.hunt_fields.empty()?360:5,{}};
            limit.suffix=" min";
            if (enemy.species=="Stump Snail") limit.label="Session length";
            result.push_back(std::move(limit));
        }
        if (enemy.species=="King Beetle" || enemy.species=="Tunnel Bear")
            result.push_back({option_key("baby-love",enemy.id),"Wait for Baby Love",options.baby_love});
        if (!enemy.hunt_fields.empty()) {
            const auto* saved=std::get_if<HuntOptions>(&options.boss);
            SettingSelection fields,selected;
            for (const auto& id:enemy.hunt_fields) {
                const auto field=game::field_at(id+".entry");const auto name=field?field->name:id;
                fields.push_back(name);
                if (!saved || std::find(saved->fields.begin(),saved->fields.end(),id)!=saved->fields.end()) selected.push_back(name);
            }
            result.push_back({"vicious-fields","Search fields",selected,0,0,fields,true});
            result.push_back({"vicious-daily","Daily bonus only",saved && saved->daily_only});
            result.back().help="After a confirmed kill, wait 22 hours. Missed searches can still retry.";
        }
        if (enemy.species=="Mondo Chick") {
            const auto* saved=std::get_if<MondoOptions>(&options.boss);
            const auto value=saved?*saved:MondoOptions{};
            result.push_back({"mondo-mode","Mode",std::string(value.mode==MondoMode::kill?"Kill":"Buff"),0,0,{"Kill","Buff"}});
            result.push_back({"mondo-seconds","Buff time",value.seconds,5,900,{}}); result.back().suffix=" sec";
            result.push_back({"mondo-loot-side","Loot side",std::string(value.direction==LootDirection::left?"Left":value.direction==LootDirection::right?"Right":"Random"),0,0,{"Random","Left","Right"}});
            result.push_back({"mondo-loot-seconds","Loot time",value.loot_seconds,1,120,{}}); result.back().suffix=" sec";
            result.push_back({"mondo-lead","Leave early",value.lead_seconds,0,300,{}}); result.back().suffix=" sec";
            result.push_back({"mondo-public","Fight in public server",false});
            result.back().help="Leave the configured private server for Mondo, then return after the visit. One trip per hourly spawn.";
        } else if (enemy.species=="Commando Chick") {
            const auto* saved=std::get_if<CommandoOptions>(&options.boss);
            const auto value=saved?*saved:CommandoOptions{};
            result.push_back({"commando-level","Level",value.level,3,25,{}});
            result.push_back({"commando-health","Starting HP",value.health,0,commando_health(25),{}});
            result.back().help="0 uses full health for the selected level. Live readings update the estimate during the fight.";
            result.push_back({"commando-session","Session",std::string(value.until_defeated?"Until defeated":"Timed"),0,0,{"Timed","Until defeated"}});
        } else if (enemy.species=="King Beetle") {
            const auto* saved=std::get_if<BeetleOptions>(&options.boss);
            result.push_back({"beetle-amulet","Amulet",std::string(!saved || saved->keep_old?"Keep old":"Choose manually"),0,0,{"Keep old","Choose manually"}});
        } else if (enemy.species=="Stump Snail") {
            const auto* saved=std::get_if<SnailOptions>(&options.boss);
            const auto value=saved?*saved:SnailOptions{};
            result.push_back({"snail-session","Session",std::string(value.until_defeated?"Until defeated":"Timed"),0,0,{"Timed","Until defeated"}});
            result.push_back({"snail-health","Starting HP",(value.health+50000)/100000,0,300,{}});
            result.back().decimals=1; result.back().suffix="M";
            result.push_back({"snail-amulet","Shell Amulet",std::string(value.keep_old?"Keep old":"Choose manually"),0,0,{"Keep old","Choose manually"}});
        }
    }
    return result;
}
using Builder=std::function<std::unique_ptr<extensions::Module>(Options,const extensions::Manifest&)>;
class CombatModule final : public extensions::Module {
    extensions::Manifest visits_;
    visits::AvailableSettings available_;
    std::vector<extensions::Setting> options_;
    struct Target { std::string id,species; bool boss; int maximum_health; std::shared_ptr<FightProgress> progress; bool hunt{}; };
    std::vector<Target> targets_;
    Builder build_;
    std::shared_ptr<extensions::Module> delegate_;
    std::shared_ptr<MondoTrip> trip_;
    std::function<Time()> clock_;
    bool public_mondo_{},interrupt_{},connected_{};
    int lead_{};
public:
    CombatModule(const extensions::Manifest& manifest,const Plan& plan,const Plan& available,Builder build,std::shared_ptr<MondoTrip> trip)
        :visits_(manifest),available_(visit_plan(plan),visit_plan(available)),options_(option_settings(plan)),build_(std::move(build)),trip_(std::move(trip)),clock_(plan.wall_clock) {
        if (manifest.settings.size()>maximum_settings) throw std::invalid_argument("Too many Combat settings");
        for (const auto& enemy:plan.enemies) targets_.push_back({enemy.id,enemy.species,enemy.boss,enemy.maximum_health,enemy.progress,!enemy.hunt_fields.empty()});
        for (const auto& expected:options_) {
            const auto found=std::find_if(manifest.settings.begin(),manifest.settings.end(),[&](const auto& s) { return s.id==expected.id; });
            if (found==manifest.settings.end() || found->initial.index()!=expected.initial.index()
                || found->minimum!=expected.minimum || found->maximum!=expected.maximum || found->choices!=expected.choices
                || std::count_if(manifest.settings.begin(),manifest.settings.end(),[&](const auto& s) { return s.id==expected.id; })!=1)
                throw std::invalid_argument("Incompatible Combat settings schema");
            if (const auto* number=std::get_if<int>(&found->initial); number && (*number<expected.minimum || *number>expected.maximum))
                throw std::invalid_argument("Invalid Combat default preference");
            std::erase_if(visits_.settings,[&](const auto& s) { return s.id==expected.id; });
        }
        visits_=available_.manifest(std::move(visits_));
        visits_.requests.clear();
        delegate_=build_(plan.options,visits_);
    }
    void start(const Settings& values) override {
        auto settings=values;
        for (const auto& expected:options_) {
            const auto found=settings.find(expected.id);
            if (found==settings.end() || found->second.index()!=expected.initial.index())
                throw std::invalid_argument("Missing Combat preferences");
            if (const auto* number=std::get_if<int>(&found->second); number && (*number<expected.minimum || *number>expected.maximum))
                throw std::invalid_argument("Invalid Combat preference");
            if (const auto* choice=std::get_if<std::string>(&found->second); choice && std::find(expected.choices.begin(),expected.choices.end(),*choice)==expected.choices.end())
                throw std::invalid_argument("Invalid Combat choice");
            if (const auto* selected=std::get_if<SettingSelection>(&found->second)) {
                std::set<std::string> unique;
                for (const auto& id:*selected) if (!unique.insert(id).second || std::find(expected.choices.begin(),expected.choices.end(),id)==expected.choices.end()) throw std::invalid_argument("Invalid boss selection");
            }
            settings.erase(found);
        }
        Options options; if (values.contains("bosses")) options.boss_order=std::get<SettingSelection>(values.at("bosses"));
        options.respawn_reduction=std::get<int>(values.at("respawn-reduction"));
        for (const auto& target:targets_) {
            const auto& id=target.id;
            if (target.boss) {
                const auto& bosses=std::get<SettingSelection>(values.at("bosses"));
                settings[features::visits::selection_key(id)]=std::find(bosses.begin(),bosses.end(),id)!=bosses.end();
            }
            auto& choice=options.enemies[id];
            choice={std::get<bool>(values.at(option_key("loot",id))),
                target.boss?Time(std::get<int>(values.at(option_key("fight-minutes",id)))*60000):Time{}};
            if (target.species=="King Beetle" || target.species=="Tunnel Bear") choice.baby_love=std::get<bool>(values.at(option_key("baby-love",id)));
            if (target.hunt) {
                HuntOptions hunt; hunt.daily_only=std::get<bool>(values.at("vicious-daily"));
                for (const auto& name:std::get<SettingSelection>(values.at("vicious-fields")))
                    for (const auto& field:game::fields()) if (field.name==name) hunt.fields.push_back(field.id);
                if (hunt.fields.empty()) throw std::invalid_argument("Choose at least one Vicious Bee search field");
                choice.boss=std::move(hunt);
            }
            if (target.species=="Mondo Chick") {
                const auto& side=std::get<std::string>(values.at("mondo-loot-side"));
                choice.boss=MondoOptions{std::get<std::string>(values.at("mondo-mode"))=="Buff"?MondoMode::buff:MondoMode::kill,
                    std::get<int>(values.at("mondo-seconds")),side=="Left"?LootDirection::left:side=="Right"?LootDirection::right:LootDirection::random,
                    std::get<int>(values.at("mondo-loot-seconds")),std::get<int>(values.at("mondo-lead"))};
            } else if (target.species=="Commando Chick") choice.boss=CommandoOptions{std::get<int>(values.at("commando-level")),std::get<std::string>(values.at("commando-session"))=="Until defeated",std::get<int>(values.at("commando-health"))};
            else if (target.species=="King Beetle") choice.boss=BeetleOptions{std::get<std::string>(values.at("beetle-amulet"))=="Keep old"};
            else if (target.species=="Stump Snail") choice.boss=SnailOptions{std::get<int>(values.at("snail-health"))*100000,
                std::get<std::string>(values.at("snail-session"))=="Until defeated",std::get<std::string>(values.at("snail-amulet"))=="Keep old"};
        }
        auto next=build_(std::move(options),visits_);
        const auto bosses=values.contains("bosses")?std::get<SettingSelection>(values.at("bosses")):SettingSelection{};
        public_mondo_=values.contains("mondo-public") && std::get<bool>(values.at("mondo-public"))
            && std::find(bosses.begin(),bosses.end(),"Mondo Chick")!=bosses.end();
        lead_=values.contains("mondo-lead")?std::get<int>(values.at("mondo-lead")):0;
        interrupt_=std::get<bool>(values.at("interrupt-gather"));
        next->start(available_.values(std::move(settings)));
        delegate_=std::move(next); connected_=false;
    }
    void update(const extensions::SessionView& view) override {
        if (!connected_) { trip_->configure(public_mondo_,lead_,interrupt_); connected_=true; }
        delegate_->update(view);
        bool due=false;
        if (trip_->selected() && !trip_->active()) {
            const auto timers=delegate_->timers();
            const auto mondo=std::find_if(timers.begin(),timers.end(),[](const auto& timer) { return timer.id=="Mondo Chick"; });
            due=mondo!=timers.end() && (!mondo->remaining || *mondo->remaining<=Time{});
        }
        trip_->update(view,clock_(),due);
    }
    std::vector<extensions::Metric> metrics() const override {
        auto result=delegate_->metrics();
        std::erase_if(result,[](const auto& metric) { return metric.label!="Status" && metric.label!="Confirmed" && metric.label!="Selected" && metric.label!="Current action"; });
        for (const auto& target:targets_) if (target.progress && target.maximum_health) {
            const auto progress=target.progress->read();
            if (!progress.health) continue;
            result.push_back({"Health",std::to_string(std::llround(*progress.health))+" HP",*progress.health/std::max(1,progress.maximum),target.id});
            if (progress.damage_per_second) result.push_back({"Damage",std::to_string(std::llround(*progress.damage_per_second))+" HP/sec",{},target.id});
            if (progress.remaining) {
                const auto minutes=(progress.remaining->count()+59999)/60000;
                result.push_back({"Estimated time",minutes>=60?std::to_string(minutes/60)+"h "+std::to_string(minutes%60)+"m":std::to_string(minutes)+" min",{},target.id});
            }
        }
        return result;
    }
    std::vector<extensions::ModuleTimer> timers() const override { return delegate_->timers(); }
    std::optional<std::vector<extensions::Metric>> report_metrics() const override { return delegate_->report_metrics(); }
    std::vector<Counter> report_counters() const override { return delegate_->report_counters(); }
    void stop() noexcept override { if (connected_) trip_->configure(false,0,false); connected_=false; if (delegate_) delegate_->stop(); }
    game::Requests requests() const override { return delegate_->requests(); }
    game::RequestBatch request_channels() const override { return trip_->requests(); }
    std::optional<extensions::RoutineParticipation> routine() const override { return delegate_->routine(); }
    std::unique_ptr<automation::Program> routine_program() const override { return program(); }
    bool executable() const noexcept override { return delegate_->executable(); }
    bool pending_work(const extensions::SessionView& view) const override { return trip_->active() || delegate_->pending_work(view); }
    std::optional<extensions::WorkEstimate> next_work(const extensions::SessionView& view) const override {
        if (trip_->active()) return extensions::WorkEstimate{"Finish server visit",{},Time{},view.now,view.now};
        return delegate_->next_work(view);
    }
    std::unique_ptr<automation::Program> program() const override { return trip_->active()?trip_->program([delegate=delegate_] { return delegate->program(); }):delegate_->program(); }
};
}
Settings migrate_settings(int version,const Settings& values,const Plan& plan) {
    if (version<1 || version>5) throw std::invalid_argument("Unknown previous Combat settings version");
    auto result=version==1?features::visits::migrate_selections(values,{"Ladybug (Mushroom)","Spider"}):values;
    result.erase("follow-requests");
    SettingSelection bosses;
    for (const auto& enemy:plan.enemies) if (enemy.boss) {
        const auto it=result.find(features::visits::selection_key(enemy.id));
        if (it!=result.end() && std::get<bool>(it->second)) bosses.push_back(enemy.id);
    }
    if (std::any_of(plan.enemies.begin(),plan.enemies.end(),[](const auto& enemy) { return enemy.boss; })) result["bosses"]=std::move(bosses);
    if (const auto old=result.find("collect-loot"); old!=result.end()) {
        if (!std::holds_alternative<bool>(old->second)) throw std::invalid_argument("Invalid previous loot preference");
        for (const auto& enemy:plan.enemies) result[option_key("loot",enemy.id)]=old->second;
        result.erase(old);
    }
    return result;
}
extensions::Manifest module_manifest(const Plan& plan,std::string id,std::string provider,bool requests) {
    auto manifest=features::visits::manifest(visit_plan(plan),std::move(id),std::move(provider),"Combat",permissions);
    manifest.setup_profiles={{"Honey farming",50,{{"interrupt-gather",false}}},
        {"Quest progress",20,{{"interrupt-gather",true}}},{"Materials",10,{{"interrupt-gather",true}}}};
    manifest.enabled_by_default=true;
    manifest.requests.insert(extensions::RequestService::servers);
    if (requests) manifest.consumes.insert(extensions::RequestService::enemies);
    for (const auto& enemy : plan.enemies) for (auto& setting : manifest.settings)
        if (setting.id==features::visits::selection_key(enemy.id)) setting.initial=enemy.selected_by_default;
    auto& sections=manifest.page->sections;
    manifest.page->compact=true;
    sections.clear();
    sections.push_back({"schedule","Options",{},{"interrupt-gather","respawn-reduction"},false,2});
    sections.push_back({"run","Run limit",{},{"run-seconds"}});
    if (std::any_of(plan.enemies.begin(),plan.enemies.end(),[](const auto& enemy) { return enemy.boss; })) {
        extensions::PageSection choices{"boss-selection","Bosses",{},{"bosses"},false,1}; choices.tab="Bosses"; sections.push_back(std::move(choices));
    }
    extensions::PageSection mobs{"targets","Mobs"}; mobs.tab="Mobs"; mobs.columns=4;
    for (const auto& enemy:plan.enemies) {
        const auto selection=features::visits::selection_key(enemy.id);
        if (!enemy.boss) {
            if (const auto field=enemy.id.find(" ("); enemy.species=="Rhino Beetle" && field!=std::string::npos)
                for (auto& setting:manifest.settings) if (setting.id==selection) setting.label="Rhino"+enemy.id.substr(field);
            mobs.settings.push_back(selection); mobs.settings.push_back(option_key("loot",enemy.id));
            continue;
        }
        for (auto& setting:manifest.settings) if (setting.id==selection) setting.hidden=true;
        extensions::PageSection boss{selection,enemy.id,{}, {selection,option_key("loot",enemy.id)},false,2};
        boss.collection="bosses"; boss.choice=enemy.id;
        if (enemy.species=="Mondo Chick") {
            boss.settings.insert(boss.settings.end(),{"mondo-mode","mondo-seconds",option_key("fight-minutes",enemy.id),"mondo-loot-side","mondo-loot-seconds","mondo-lead","mondo-public"});
        } else if (enemy.species=="Stump Snail") {
            boss.settings.insert(boss.settings.end(),{"snail-session",option_key("fight-minutes",enemy.id),"snail-health","snail-amulet"});
        } else {
            boss.settings.push_back(option_key("fight-minutes",enemy.id));
            if (enemy.species=="Commando Chick") boss.settings.insert(boss.settings.end(),{"commando-level","commando-health","commando-session"});
            if (enemy.species=="King Beetle") boss.settings.push_back("beetle-amulet");
            if (enemy.species=="King Beetle" || enemy.species=="Tunnel Bear") boss.settings.push_back(option_key("baby-love",enemy.id));
            if (!enemy.hunt_fields.empty()) boss.settings.insert(boss.settings.end(),{"vicious-daily","vicious-fields"});
        }
        sections.push_back(std::move(boss));
    }
    if (!mobs.settings.empty()) sections.insert(sections.begin()+2,std::move(mobs));
    sections.push_back({"overview","Progress",{},{},true,2,true});
    for (auto& section:sections) section.inline_labels=true;
    for (auto setting:option_settings(plan)) manifest.settings.push_back(std::move(setting));
    for (const auto& enemy:plan.enemies) if (!enemy.boss)
        for (auto& setting:manifest.settings) if (setting.id==option_key("loot",enemy.id)) setting.enabled_by=features::visits::selection_key(enemy.id);
    if (manifest.settings.size()>maximum_settings) throw std::invalid_argument("Too many Combat settings");
    return manifest;
}
extensions::Factory module_factory(Plan plan,automation::NavigationServices navigation,std::shared_ptr<const automation::AvailabilitySource> availability,std::shared_ptr<const game::EnemyGoalSource> goals,std::shared_ptr<visits::Cooldowns> saved,std::optional<Plan> configuration) {
    features::visits::Demand demand;
    if (goals) {
        for (auto& enemy:plan.enemies) if (!enemy.hunt_fields.empty())
            enemy.requested_field=[goals,species=enemy.species](const ObservationContext& c) {
                std::optional<game::EnemyGoal> best;
                for (const auto& goal:goals->enemies(c.now,c.generation))
                    if (goal.enemy==species && !goal.field.empty() && goal.generation==c.generation && goal.expires>c.now
                        && (!best || goal.priority>best->priority)) best=goal;
                return best?best->field:std::string{};
            };
        std::map<std::string,std::string> types;
        for (const auto& enemy : plan.enemies) if (!enemy.species.empty()) types.emplace(enemy.id,enemy.species);
        demand=[goals=std::move(goals),types=std::move(types)](const ObservationContext& c) {
            features::visits::RequestedVisits requested;
            for (const auto& goal : goals->enemies(c.now,c.generation)) {
                if (goal.generation!=c.generation || goal.expires<=c.now) continue;
                for (const auto& [id,species] : types) if (goal.enemy==species) requested[id].insert(goal.owner);
            }
            return requested;
        };
    }
    module_manifest(plan,"hive.combat","native-combat",bool(demand));
    features::visits::factory(visit_plan(plan),navigation,availability,permissions,demand);
    if (configuration) for (auto& enemy:configuration->enemies) {
        const auto available=std::find_if(plan.enemies.begin(),plan.enemies.end(),[&](const auto& item) { return item.id==enemy.id; });
        if (available!=plan.enemies.end()) enemy=*available;
    }
    const auto display=std::make_shared<const Plan>(configuration?std::move(*configuration):plan);
    const auto defaults=std::make_shared<const Plan>(std::move(plan));
    return [display,defaults,navigation=std::move(navigation),availability=std::move(availability),demand=std::move(demand),saved=std::move(saved)](const extensions::Manifest& manifest) {
      auto trip=std::make_shared<MondoTrip>(manifest.id);
      Builder build=[defaults,navigation,availability=server_availability(availability,trip),demand,saved](Options options,const extensions::Manifest& manifest) {
        auto configured=*defaults;
        const auto available=[&](const auto& id) { return std::any_of(configured.enemies.begin(),configured.enemies.end(),[&](const auto& enemy) { return enemy.id==id; }); };
        std::erase_if(options.enemies,[&](const auto& item) { return !available(item.first); });
        std::erase_if(options.boss_order,[&](const auto& id) { return !available(id); });
        configured.options=std::move(options);
        return features::visits::factory(visit_plan(std::move(configured)),navigation,availability,permissions,demand,saved)(manifest);
      };
      return std::make_unique<CombatModule>(manifest,*display,*defaults,std::move(build),std::move(trip));
    };
}
}
