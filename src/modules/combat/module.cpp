#include "hive/modules/combat/module.hpp"
#include <algorithm>
#include <cmath>

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
        if (enemy.species=="Mondo Chick") {
            const auto* saved=std::get_if<MondoOptions>(&options.boss);
            const auto value=saved?*saved:MondoOptions{};
            result.push_back({"mondo-mode","Mode",std::string(value.mode==MondoMode::kill?"Kill":"Buff"),0,0,{"Kill","Buff"}});
            result.push_back({"mondo-seconds","Buff time",value.seconds,5,900,{}}); result.back().suffix=" sec";
            result.push_back({"mondo-loot-side","Loot side",std::string(value.direction==LootDirection::left?"Left":value.direction==LootDirection::right?"Right":"Random"),0,0,{"Random","Left","Right"}});
            result.push_back({"mondo-loot-seconds","Loot time",value.loot_seconds,1,120,{}}); result.back().suffix=" sec";
        } else if (enemy.species=="Commando Chick") {
            const auto* saved=std::get_if<CommandoOptions>(&options.boss);
            const auto value=saved?*saved:CommandoOptions{};
            result.push_back({"commando-level","Level",value.level,3,25,{}});
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
    std::vector<extensions::Setting> options_;
    struct Target { std::string id,species; bool boss; int maximum_health; std::shared_ptr<FightProgress> progress; };
    std::vector<Target> targets_;
    Builder build_;
    std::unique_ptr<extensions::Module> delegate_;
public:
    CombatModule(const extensions::Manifest& manifest,const Plan& plan,Builder build)
        :visits_(manifest),options_(option_settings(plan)),build_(std::move(build)) {
        if (manifest.settings.size()>maximum_settings) throw std::invalid_argument("Too many Combat settings");
        for (const auto& enemy:plan.enemies) targets_.push_back({enemy.id,enemy.species,enemy.boss,enemy.maximum_health,enemy.progress});
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
        visits_.page.reset();
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
            if (target.species=="Mondo Chick") {
                const auto& side=std::get<std::string>(values.at("mondo-loot-side"));
                choice.boss=MondoOptions{std::get<std::string>(values.at("mondo-mode"))=="Buff"?MondoMode::buff:MondoMode::kill,
                    std::get<int>(values.at("mondo-seconds")),side=="Left"?LootDirection::left:side=="Right"?LootDirection::right:LootDirection::random,
                    std::get<int>(values.at("mondo-loot-seconds"))};
            } else if (target.species=="Commando Chick") choice.boss=CommandoOptions{std::get<int>(values.at("commando-level")),std::get<std::string>(values.at("commando-session"))=="Until defeated"};
            else if (target.species=="King Beetle") choice.boss=BeetleOptions{std::get<std::string>(values.at("beetle-amulet"))=="Keep old"};
            else if (target.species=="Stump Snail") choice.boss=SnailOptions{std::get<int>(values.at("snail-health"))*100000,
                std::get<std::string>(values.at("snail-session"))=="Until defeated",std::get<std::string>(values.at("snail-amulet"))=="Keep old"};
        }
        auto next=build_(std::move(options),visits_);
        next->start(settings);
        delegate_=std::move(next);
    }
    void update(const extensions::SessionView& view) override { delegate_->update(view); }
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
    void stop() noexcept override { delegate_->stop(); }
    game::Requests requests() const override { return delegate_->requests(); }
    game::RequestBatch request_channels() const override { return delegate_->request_channels(); }
    std::optional<extensions::RoutineParticipation> routine() const override { return delegate_->routine(); }
    std::unique_ptr<automation::Program> routine_program() const override { return delegate_->routine_program(); }
    bool executable() const noexcept override { return delegate_->executable(); }
    bool pending_work(const extensions::SessionView& view) const override { return delegate_->pending_work(view); }
    std::unique_ptr<automation::Program> program() const override { return delegate_->program(); }
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
        extensions::PageSection boss{selection,enemy.id,{}, {selection,option_key("loot",enemy.id)},false,3};
        boss.collection="bosses"; boss.choice=enemy.id;
        if (enemy.species=="Mondo Chick") {
            boss.settings.insert(boss.settings.end(),{"mondo-mode","mondo-seconds",option_key("fight-minutes",enemy.id),"mondo-loot-side","mondo-loot-seconds"});
        } else if (enemy.species=="Stump Snail") {
            boss.settings.insert(boss.settings.end(),{"snail-session",option_key("fight-minutes",enemy.id),"snail-health","snail-amulet"});
        } else {
            boss.settings.push_back(option_key("fight-minutes",enemy.id));
            if (enemy.species=="Commando Chick") boss.settings.insert(boss.settings.end(),{"commando-level","commando-session"});
            if (enemy.species=="King Beetle") boss.settings.push_back("beetle-amulet");
        }
        sections.push_back(std::move(boss));
    }
    if (!mobs.settings.empty()) sections.insert(sections.begin()+2,std::move(mobs));
    sections.push_back({"overview","Progress",{},{},true,2,true});
    for (auto setting:option_settings(plan)) manifest.settings.push_back(std::move(setting));
    if (manifest.settings.size()>maximum_settings) throw std::invalid_argument("Too many Combat settings");
    return manifest;
}
extensions::Factory module_factory(Plan plan,automation::NavigationServices navigation,std::shared_ptr<const automation::AvailabilitySource> availability,std::shared_ptr<const game::EnemyGoalSource> goals,std::shared_ptr<visits::Cooldowns> saved) {
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
            std::set<std::string> requested;
            for (const auto& goal : goals->enemies(c.now,c.generation)) {
                if (goal.generation!=c.generation || goal.expires<=c.now) continue;
                for (const auto& [id,species] : types) if (goal.enemy==species) requested.insert(id);
            }
            return requested;
        };
    }
    module_manifest(plan,"hive.combat","native-combat",bool(demand));
    features::visits::factory(visit_plan(plan),navigation,availability,permissions,demand);
    const auto defaults=std::make_shared<const Plan>(std::move(plan));
    Builder build=[defaults,navigation=std::move(navigation),availability=std::move(availability),demand=std::move(demand),saved=std::move(saved)](Options options,const extensions::Manifest& manifest) {
        auto configured=*defaults; configured.options=std::move(options);
        return features::visits::factory(visit_plan(std::move(configured)),navigation,availability,permissions,demand,saved)(manifest);
    };
    return [defaults,build=std::move(build)](const extensions::Manifest& manifest) { return std::make_unique<CombatModule>(manifest,*defaults,build); };
}
}
