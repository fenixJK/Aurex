#include "module.hpp"
#include "drops.hpp"
#include <algorithm>
#include <future>
#include "presentation.hpp"

namespace hive::features::planters {
namespace {
const automation::Permissions permissions{automation::Permission::movement,automation::Permission::interact};
int integer(const Settings& settings,const std::string& key,int high,int low=1) {
    const auto it=settings.find(key);
    if (it==settings.end() || !std::holds_alternative<int>(it->second) || std::get<int>(it->second)<low || std::get<int>(it->second)>high)
        throw std::invalid_argument("Invalid planter setting: "+key);
    return std::get<int>(it->second);
}
bool has_loot(const ExecutionPlan& plan) {
    return std::any_of(plan.visits.begin(),plan.visits.end(),[](const auto& entry) { return !entry.second.loot.empty(); });
}
SettingSelection return_choices(const ExecutionPlan& plan) {
    if (plan.destinations.empty()) return {};
    const auto all=[&](bool walking) {
        return std::all_of(plan.destinations.begin(),plan.destinations.end(),[&](const auto& item) {
            const auto visit=plan.visits.find(item.first);
            return visit!=plan.visits.end() && bool(walking?visit->second.walking_departure:visit->second.departure);
        });
    };
    if (!all(true)) return {};
    return all(false)?SettingSelection{"Walk","Reset"}:SettingSelection{"Walk"};
}
std::string phase(ExecutionStage stage) {
    switch (stage) {
    case ExecutionStage::ready: return "Ready"; case ExecutionStage::planning: return "Planning";
    case ExecutionStage::preparing: return "Preparing travel";
    case ExecutionStage::travelling: return "Travelling"; case ExecutionStage::checking: return "Checking field";
    case ExecutionStage::acting: return "Using planter"; case ExecutionStage::confirming: return "Confirming outcome";
    case ExecutionStage::reconciling: return "Checking previous action"; case ExecutionStage::finished: return "Finished";
    case ExecutionStage::looting: return "Collecting planter loot"; case ExecutionStage::returning: return "Returning";
    case ExecutionStage::checking_backpack: return "Checking backpack"; case ExecutionStage::converting: return "Converting honey";
    case ExecutionStage::locating: return "Locating planter";
    }
    throw std::logic_error("Invalid planter phase");
}
class PlanterModule final : public extensions::Module {
    ExecutionPlan defaults_,plan_;
    std::shared_ptr<const game::RouteNetwork> network_;
    std::shared_ptr<Account> account_;
    std::shared_ptr<const Source> source_;
    std::shared_ptr<const Actions> actions_;
    WallClock clock_;
    std::function<ObservationContext()> context_;
    mutable std::shared_ptr<const ExecutionResults> results_;
    mutable Counters counters_;
    mutable std::uint64_t report_run_{};
    bool started_{},gather_fields_{},other_running_{},interrupt_gather_{true};
    mutable bool check_requested_{true};
    mutable std::uint64_t timer_revision_{};
    mutable Time material_checked_{};
    std::uint64_t generation_{};
    std::string id_;
    game::Requests requests_;
    unsigned simulation_days_{7};
    std::shared_ptr<std::atomic_bool> cancel_preview_{std::make_shared<std::atomic_bool>(false)};
    std::future<std::shared_ptr<const extensions::Plot>> preview_;
    std::shared_ptr<const extensions::Plot> plot_;
    std::string preview_status_;
    std::optional<Time> material_ready(std::size_t growing) const {
        if (!plan_.preferences || plan_.preferences->materials.empty() || growing>=plan_.capacity) return {};
        MaterialPlan materials; materials.history=account_->materials();
        apply_preferences(materials,*plan_.preferences);
        return next_material_ready(materials,material_checked_);
    }
public:
    PlanterModule(const extensions::Manifest& manifest,ExecutionPlan defaults,std::shared_ptr<const game::RouteNetwork> network,
        std::shared_ptr<Account> account,std::shared_ptr<const Source> source,std::shared_ptr<const Actions> actions,WallClock clock,std::function<ObservationContext()> context)
        :defaults_(std::move(defaults)),network_(std::move(network)),account_(std::move(account)),source_(std::move(source)),actions_(std::move(actions)),clock_(std::move(clock)),context_(std::move(context)),id_(manifest.id) {
        const auto expected=module_manifest(defaults_);
        if (manifest.id.empty() || manifest.permissions!=permissions || manifest.requests!=expected.requests || !manifest.scripts.empty()
            || !manifest.sources.empty() || !manifest.bindings.empty() || manifest.settings.size()!=expected.settings.size())
            throw std::invalid_argument("Incompatible planter module manifest");
        std::set<std::string> seen;
        for (const auto& setting : manifest.settings) {
            const auto it=std::find_if(expected.settings.begin(),expected.settings.end(),[&](const auto& s) { return s.id==setting.id; });
            if (it==expected.settings.end() || !seen.insert(setting.id).second || setting.initial.index()!=it->initial.index()
                || setting.minimum!=it->minimum || setting.maximum!=it->maximum || setting.choices!=it->choices)
                throw std::invalid_argument("Incompatible planter settings schema");
            if (const auto* number=std::get_if<int>(&setting.initial)) {
                if (*number<setting.minimum || *number>setting.maximum) throw std::invalid_argument("Invalid planter default");
            } else if (const auto* selected=std::get_if<SettingSelection>(&setting.initial)) {
                if (selected->size()>setting.choices.size() || std::set<std::string>(selected->begin(),selected->end()).size()!=selected->size()
                    || std::any_of(selected->begin(),selected->end(),[&](const auto& item) {
                        return std::find(setting.choices.begin(),setting.choices.end(),item)==setting.choices.end();
                    })) throw std::invalid_argument("Invalid planter default selection");
            }
        }
    }
    void start(const Settings& saved) override {
        cancel_preview_->store(true); if(preview_.valid()) preview_.wait();
        plot_.reset(); preview_status_.clear();
        auto values=saved; values.try_emplace("materials",SettingSelection{}); values.try_emplace("interrupt-gather",true);
        values.try_emplace("mode",std::string("Nectar")); values.try_emplace("secondary",false);
        if(!std::holds_alternative<bool>(values.at("interrupt-gather"))) throw std::invalid_argument("Invalid gather interrupt setting");
        interrupt_gather_=std::get<bool>(values.at("interrupt-gather")); values.try_emplace("simulation-days",7); simulation_days_=integer(values,"simulation-days",30,3);
        values.try_emplace("gather-planter-fields",false);
        const auto* gather=std::get_if<bool>(&values.at("gather-planter-fields"));
        if (!gather) throw std::invalid_argument("Invalid planter gathering setting");
        gather_fields_=*gather; requests_={};
        if (values.size()!=module_manifest(defaults_).settings.size()) throw std::invalid_argument("Unexpected planter settings");
        auto plan=defaults_; plan.config.duration=Time(integer(values,"run-seconds",28800)*1000);
        const auto returns=return_choices(plan);
        if (!returns.empty()) {
            const auto it=values.find("return-method");
            if (it==values.end() || !std::holds_alternative<std::string>(it->second)
                || std::find(returns.begin(),returns.end(),std::get<std::string>(it->second))==returns.end())
                throw std::invalid_argument("Invalid planter return method");
            plan.walk_return=std::get<std::string>(it->second)=="Walk";
        }
        plan.capacity=integer(values,"capacity",3); plan.max_actions=integer(values,"max-actions",32);
        if (has_loot(plan)) {
            const auto it=values.find("collect-loot");
            if (it==values.end() || !std::holds_alternative<bool>(it->second)) throw std::invalid_argument("Missing planter loot selection");
            plan.collect_loot=std::get<bool>(it->second);
        }
        if (plan.conversion) {
            const auto it=values.find("convert-after-harvest");
            if (it==values.end() || !std::holds_alternative<bool>(it->second)) throw std::invalid_argument("Missing planter conversion selection");
            plan.conversion->enabled=plan.conversion->every_return || std::get<bool>(it->second);
            plan.conversion->threshold=integer(values,"conversion-threshold",100);
        }
        auto preferences=defaults_.preferences.value_or(default_preferences());
        const auto* mode=std::get_if<std::string>(&values.at("mode"));
        const auto* secondary=std::get_if<bool>(&values.at("secondary"));
        if (!mode || (*mode!="Nectar" && *mode!="Items") || !secondary) throw std::invalid_argument("Invalid planter priority");
        preferences.mode=*mode=="Items"?Mode::items:Mode::nectar; preferences.secondary=*secondary;
        const auto selection=[&](const char* id) -> const SettingSelection& {
            const auto it=values.find(id);
            if (it==values.end() || !std::holds_alternative<SettingSelection>(it->second)) throw std::invalid_argument("Missing planter selection");
            const auto& items=std::get<SettingSelection>(it->second);
            if (items.size()>std::max<std::size_t>(20,item_goals().size()) || std::set<std::string>(items.begin(),items.end()).size()!=items.size()) throw std::invalid_argument("Invalid planter selection");
            return items;
        };
        preferences.planters.clear();
        for (const auto& label : selection("equipment")) {
            const auto item=std::find_if(equipment().begin(),equipment().end(),[&](const auto& item) { return item.label==label; });
            if (item==equipment().end()) throw std::invalid_argument("Unknown planter selection");
            preferences.planters.insert(item->id);
        }
        const auto& fields=selection("fields"); preferences.fields={fields.begin(),fields.end()};
        for (const auto& field : fields) if (!plan.destinations.contains(field)) throw std::invalid_argument("Planter field has no connected route");
        for (std::size_t i=0;i<5;++i) {
            preferences.targets[i]={double(integer(values,"target-"+std::to_string(i),100,0)),double(integer(values,"buffer-"+std::to_string(i),20,0))};
            preferences.priorities[i]=integer(values,"priority-"+std::to_string(i),100,0);
        }
        preferences.materials.clear();
        for(const auto& label:selection("materials")) {
            const auto goals=item_goals();const auto item=std::find_if(goals.begin(),goals.end(),[&](const auto& goal) {return goal.label==label;});
            if(item==goals.end()) throw std::invalid_argument("Unknown material goal");
            preferences.materials.push_back(item->id);
        }
        validate(preferences); plan.preferences=std::move(preferences);
        planters::program(plan,network_,account_,source_,actions_,clock_); plan_=std::move(plan); results_.reset(); started_=true; check_requested_=true;
    }
    void update(const extensions::SessionView& view) override {
        account_->connection(view.reconnecting,clock_());
        if (generation_!=view.generation || view.status.state!=RunState::running) {
            generation_=view.generation; check_requested_=true; other_running_=false;
        }
        if (view.feature && view.feature->module_id!=id_) {
            if (view.feature->state==automation::State::running) other_running_=true;
            else if (other_running_ && automation::can_handoff(view.feature->state)) {
                if (view.feature->state==automation::State::completed && view.feature->steps>0 && account_->plants().size()<plan_.capacity) check_requested_=true;
                other_running_=false;
            }
        }
        if (!started_) throw std::logic_error("Planter module is stopped");
        if(preview_.valid() && preview_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try { plot_=preview_.get(); preview_status_="Ready"; }
            catch(const std::exception& e) { preview_status_=e.what(); }
        }
        requests_={view.generation,requests_.revision+1,{}};
        if (!gather_fields_ || !view.generation || view.status.state!=RunState::running || !plan_.preferences) return;
        const auto plants=account_->plants();
        for (const auto& plant:plants) {
            const auto nectar=static_cast<std::size_t>(plant.stats.nectar);
            if (!plan_.preferences->fields.contains(plant.stats.field) || !plan_.preferences->targets[nectar].minimum) continue;
            requests_.fields.push_back({"sipping-"+std::to_string(plant.id),plant.stats.field,10,view.now+Time(5000)});
        }
    }
    std::vector<extensions::Command> commands() const override {
        extensions::Command timers{"timers","Planter timers",{},false,false,{},{},true,"Show planter locations, growth and harvest times","planters"};
        timers.access=extensions::CommandAccess::read;timers.visible=false;
        extensions::CommandArgument field{"field","Field","Only show this field",extensions::ArgumentType::text,false,{},1,100};
        for (const auto& [name,_]:defaults_.destinations) field.choices.push_back(name);
        timers.arguments.push_back(std::move(field));
        extensions::Command check{"check","Check planters",{},false,false};
        check.access=extensions::CommandAccess::queued;check.visible=false;
        extensions::Command forecast{"forecast","Nectar prediction",{},false,false,{},{},true,"Predict nectar levels and time to target with your current planters","nectar-predict"};
        forecast.access=extensions::CommandAccess::read; forecast.visible=false;
        forecast.arguments={{"hours","Hours","Hours ahead to predict (default 24)",extensions::ArgumentType::integer,false,24,1,72}};
        return {{"simulate","Run","Simulation"},std::move(timers),std::move(check),std::move(forecast)};
    }
    extensions::CommandResult execute_command(const std::string& id,const extensions::CommandInput& input) override {
        if (id=="forecast") {
            if (!started_ || !plan_.preferences || !context_) throw std::runtime_error("Enable Planters and run the macro before predicting nectar");
            const auto context=context_();
            const auto reading=source_->read({{},0,{},context}).nectar;
            if (!reading.has_value(context,Time(2000))) throw std::runtime_error("No recent nectar reading. Try again with the buff bar visible.");
            auto groups=reading.value;
            for (auto& group:groups) {
                group.current=std::max(0.,group.current-(context.now-reading.captured).count()/864000.);
                std::erase_if(group.candidates,[&](const auto& candidate) { return !plan_.destinations.contains(candidate.stats.field); });
            }
            auto snapshot=account_->forecast_state(std::move(groups),*plan_.preferences,clock_(),plan_.capacity);
            const auto hours=std::get<int>(input.arguments.at("hours"));
            extensions::CommandResult result{"Nectar prediction"};
            result.deferred=[snapshot=std::move(snapshot),hours]() mutable {
                extensions::CommandResult reply{"Nectar prediction","Estimated from current readings and saved planters. The graph shows natural decay between harvests."};
                reply.chart=forecast_plot(std::move(snapshot),hours); return reply;
            };
            return result;
        }
        if (id=="timers") {
            extensions::CommandResult result{"Planters"};result.timers=timers();
            if (const auto field=input.arguments.find("field");field!=input.arguments.end())
                std::erase_if(result.timers,[&](const auto& timer){return timer.label!=std::get<std::string>(field->second);});
            if(result.timers.empty())result.message="No saved planters for this selection.";
            return result;
        }
        if (id=="check") { check_requested_=true;return {"Planters","Planter check queued."}; }
        return extensions::Module::execute_command(id,input);
    }
    std::shared_ptr<const extensions::Plot> plot() const override { return plot_; }
    extensions::PlotProgress plot_progress() const override { return {preview_status_=="Running",preview_status_=="Running"?"Simulating…":preview_status_}; }
    std::map<std::string,std::string> setting_summaries() const override {
        std::map<std::string,std::string> result;
        for (std::size_t i=0;i<5;++i) {
            const auto& target=plan_.preferences->targets[i];
            const auto band=bounds(target);
            result["nectar-"+std::to_string(i)]=!plan_.preferences->needs_nectar() || target.minimum==0?"Not maintained":
                (plan_.preferences->mode==Mode::items?"Spare slots · ":"Band ")+std::to_string(int(std::lround(band.lower)))+"–"+std::to_string(int(std::lround(band.upper)))+"%";
        }
        if(plan_.preferences && !plan_.preferences->materials.empty()) {
            std::string detail;
            MaterialPlan material;apply_preferences(material,*plan_.preferences);material.history=account_->materials();
            const auto now=clock_().time_since_epoch();
            for(const auto& goal:plan_.preferences->materials) {
                if(!detail.empty()) detail+="\n";
                detail+=item_label(goal)+": ";
                std::string next;bool completed=false;std::optional<Time> ready;
                for(const auto& route:material_routes()) if(route.goal==goal && !material.excluded_routes.contains(route.id)) {
                    const auto found=material.history.find(route.id);const auto progress=found==material.history.end()?MaterialProgress{}:found->second;
                    if(progress.complete) {completed=true;continue;}
                    if(progress.ready>now) {if(!ready || progress.ready<*ready) ready=progress.ready;continue;}
                    next=route.fields[progress.step]+" ("+std::to_string(progress.step+1)+"/"+std::to_string(route.fields.size())+")";
                    if(progress.step) break;
                }
                if(!next.empty()) detail+="bonus: "+next;
                else if(completed) detail+="completed";
                else {
                    bool available=false;
                    for(const auto& stats:catalog()) if(plan_.preferences->planters.contains(stats.planter) && plan_.preferences->fields.contains(stats.field)
                        && item_source(goal,stats).preference) {available=true;break;}
                    detail+=available?"regular drops":"no available source";
                    if(ready) detail+=" · bonus in ~"+std::to_string(std::max<Time::rep>(1,(*ready-now).count()/3600000))+"h";
                }
            }
            result["material-progress"]=std::move(detail);
            result["mode"]=plan_.preferences->farms_items()?std::to_string(plan_.preferences->materials.size())+" item goals selected":"Item farming off";
        } else if(plan_.preferences && plan_.preferences->farms_items()) result["mode"]="Choose item goals to farm.";
        return result;
    }
    void invoke(const std::string& command) override {
        if(command!="simulate" || !started_ || !plan_.preferences) throw std::invalid_argument("Simulation is unavailable");
        if(preview_.valid() && preview_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
        cancel_preview_=std::make_shared<std::atomic_bool>(false); plot_.reset(); preview_status_="Running";
        preview_=std::async(std::launch::async,[preferences=*plan_.preferences,capacity=plan_.capacity,days=simulation_days_,cancel=cancel_preview_] {
            return simulation_plot(preferences,capacity,days,cancel);
        });
    }
    game::Requests requests() const override { return requests_; }
    std::vector<extensions::Metric> metrics() const override {
        if (!started_ && !results_) return {{"Status","Stopped"}};
        if (!results_) return {{"Status","Ready"},{"Capacity",std::to_string(plan_.capacity)},{"Simulation",preview_status_.empty()?"Not run":preview_status_}};
        const auto report=results_->read(); auto status=phase(report.stage);
        if (!started_) status="Stopped";
        else if (report.state==automation::State::failed) status="Stopped after an error";
        else if (report.state==automation::State::deferred) status="Deferred until the next check";
        else if (report.state==automation::State::paused) status="Paused";
        else if (report.state==automation::State::stopped) status="Stopped";
        std::vector<extensions::Metric> result{{"Status",status},{"Growing",std::to_string(report.growing)},
            {"Confirmed actions",std::to_string(report.confirmed)},{"Pending action",report.pending?std::to_string(report.pending->ticket):"None"}};
        for (const auto& [id,name] : report.records) result.push_back({"Saved planter "+std::to_string(id),name});
        if (!preview_status_.empty()) result.push_back({"Simulation",preview_status_});
        if (!report.field.empty()) { result.push_back({"Field",report.field}); result.push_back({"Planter",report.planter}); }
        return result;
    }
    std::vector<Counter> report_counters() const override {
        const auto report=results_?results_->read():ExecutionReport{};
        counters_.observe("harvests","Harvested",report.harvests,report_run_,"Harvested");
        counters_.observe("placements","Placed",report.placements,report_run_,"Placed");
        return counters_.values();
    }
    std::vector<Threshold> report_thresholds() const override {
        std::vector<Threshold> result;
        if (plan_.preferences && plan_.preferences->mode==Mode::nectar) for (std::size_t i=0;i<5;++i) {
            const auto target=plan_.preferences->targets[i];
            if (target.minimum>0) result.push_back({"nectar/"+nectar_names()[i],bounds(target).lower});
        }
        return result;
    }
    std::vector<extensions::ModuleTimer> timers() const override {
        const auto ledger=account_->saved(); const auto now=clock_();
        auto plants=ledger.plants;
        if (ledger.pending && ledger.pending->operation==Operation::place) plants.push_back(ledger.pending->plant);
        std::vector<extensions::ModuleTimer> result;
        for (const auto& plant:plants) {
            const bool pending=ledger.pending && ledger.pending->plant.id==plant.id;
            auto name=plant.stats.planter;
            for(const auto& item:equipment())if(item.id==name) {name=item.label;break;}
            result.push_back({std::to_string(plant.id),plant.stats.field,name,
                pending?std::optional<Time>{}:std::max(Time{},ready_at(plant)-now),
                pending?std::optional<double>{}:std::clamp(estimated_age(plant,now)/plant.stats.full_seconds*100.,0.,100.)});
        }
        return result;
    }
    std::optional<std::vector<extensions::Metric>> report_metrics() const override {
        const auto plants=account_->plants(); const auto now=clock_();
        std::vector<extensions::Metric> result;
        for (const auto& plant:plants) {
            const auto minutes=std::max<std::int64_t>(0,(ready_at(plant)-now+std::chrono::seconds(59)).count()/60000);
            const auto time=minutes>=60?std::to_string(minutes/60)+"h "+std::to_string(minutes%60)+"m":minutes?std::to_string(minutes)+"m":"Ready";
            std::string name=plant.stats.planter;
            for (std::size_t i=1;i<name.size();++i) if (name[i]>='A' && name[i]<='Z' && name[i-1]!=' ') name.insert(i++,1,' ');
            result.push_back({plant.stats.field,name+" · "+time,std::clamp(estimated_age(plant,now)/plant.stats.full_seconds*100.,0.,100.),
                {},nectar_names()[static_cast<std::size_t>(plant.stats.nectar)]});
        }
        return result;
    }
    void stop() noexcept override { cancel_preview_->store(true); started_=false; requests_={}; }
    std::optional<extensions::RoutineParticipation> routine() const override { return extensions::RoutineParticipation{90,Time{},false,true,true,interrupt_gather_}; }
    bool pending_work(const extensions::SessionView&) const override {
        if (!started_) return false;
        if (check_requested_ || timer_revision_!=account_->edit_revision()) return true;
        const auto now=clock_();
        const auto plants=account_->plants();
        if (const auto ready=material_ready(plants.size());ready && *ready<=now.time_since_epoch()) return true;
        if (plants.empty()) return false;
        return std::any_of(plants.begin(),plants.end(),[&](const auto& plant) { return ready_at(plant)<=now; });
    }
    std::optional<extensions::WorkEstimate> next_work(const extensions::SessionView& view) const override {
        if (!started_) return {};
        if (check_requested_ || timer_revision_!=account_->edit_revision())
            return extensions::WorkEstimate{"Check planter slots",{},Time{},view.now,view.now};
        const auto plants=account_->plants();
        const auto material=material_ready(plants.size());
        const auto wall=clock_();
        if (material && (plants.empty() || std::all_of(plants.begin(),plants.end(),[&](const auto& plant) { return *material<ready_at(plant).time_since_epoch(); })))
            return extensions::WorkEstimate{"Planter bonus ready",{},Time{},view.now+std::max(Time{},*material-wall.time_since_epoch()),{}};
        if (plants.empty()) return {};
        const auto next=std::min_element(plants.begin(),plants.end(),[](const auto& a,const auto& b) { return ready_at(a)<ready_at(b); });
        const auto ready=view.now+std::max(Time{},ready_at(*next)-clock_());
        auto name=next->stats.planter;
        for (const auto& item:equipment()) if (item.id==name) { name=item.label; break; }
        const auto destination=plan_.destinations.find(next->stats.field);
        return extensions::WorkEstimate{"Harvest "+name+" in "+next->stats.field,destination!=plan_.destinations.end()?destination->second:std::string{},Time{},ready,ready};
    }
    bool executable() const noexcept override { return true; }
    std::unique_ptr<automation::Program> program() const override {
        if (!started_) throw std::logic_error("Enable Planters before starting it");
        check_requested_=false;
        material_checked_=clock_().time_since_epoch();
        timer_revision_=account_->edit_revision();
        auto run=planters::program(plan_,network_,account_,source_,actions_,clock_);
        report_counters(); ++report_run_; results_=run.results; return std::move(run.program);
    }
};
}
Settings migrate_settings(int version,const Settings& previous) {
    if(version<1 || version>2) throw std::invalid_argument("Unknown previous Planter settings version");
    auto values=previous; values.erase("resolved-action");
    const auto mode=values.find("mode"),maintain=values.find("maintain-nectar"),goals=values.find("materials");
    const bool items=mode!=values.end() && std::get<std::string>(mode->second)=="Items";
    const bool guarded=maintain!=values.end() && std::get<bool>(maintain->second);
    values["mode"]=std::string(items && !guarded?"Items":"Nectar");
    values["secondary"]=items?guarded:goals!=values.end() && !std::get<SettingSelection>(goals->second).empty();
    values.erase("maintain-nectar"); return values;
}
extensions::Manifest module_manifest(const ExecutionPlan& plan,std::string id,std::string provider) {
    if (plan.config.duration<Time(1000) || plan.config.duration>std::chrono::hours(8) || plan.config.duration.count()%1000
        || plan.capacity<1 || plan.capacity>3 || plan.max_actions<1 || plan.max_actions>32) throw std::invalid_argument("Invalid planter module defaults");
    if (plan.conversion && (!plan.return_to || !std::isfinite(plan.conversion->threshold) || plan.conversion->threshold<1
        || plan.conversion->threshold>100 || plan.conversion->threshold!=std::floor(plan.conversion->threshold)))
        throw std::invalid_argument("Invalid planter conversion defaults");
    extensions::Manifest result;
    result.requests={extensions::RequestService::fields};
    result.id=std::move(id); result.provider=std::move(provider); result.name="Planters"; result.version="0.2.0";
    result.settings_version=3;
    result.enabled_by_default=true;
    result.setup_profiles={{"Honey farming",10,{{"interrupt-gather",true}}},
        {"Quest progress",40,{{"interrupt-gather",true}}},{"Materials",30,{{"interrupt-gather",true}}}};
    result.description="Prioritize nectar or item drops, with optional work in spare capacity."; result.permissions=permissions;
    result.settings={{"run-seconds","Run limit (seconds)",static_cast<int>(plan.config.duration.count()/1000),1,28800,{}},
        {"capacity","Slots",static_cast<int>(plan.capacity),1,3,{}},{"max-actions","Actions per run",static_cast<int>(plan.max_actions),1,32,{}}};
    const auto preferences=plan.preferences.value_or(default_preferences()); validate(preferences);
    result.settings.push_back({"mode","Priority",std::string(preferences.mode==Mode::items?"Items":"Nectar"),0,0,{"Nectar","Items"}});
    result.settings.push_back({"secondary","Also farm items",preferences.secondary});
    result.settings.back().enabled_by="mode"; result.settings.back().enabled_values={"Nectar","Items"};
    result.settings.back().dependent_labels={{"Nectar","Also farm items"},{"Items","Use spare slots for nectar"}};
    result.settings.back().help="Nectar first: fit items around your nectar bands. Items first: use remaining slots for nectar without delaying item routes.";
    SettingSelection labels,selected,fields;
    for (const auto& item : equipment()) { labels.push_back(item.label); if (preferences.planters.contains(item.id)) selected.push_back(item.label); }
    const auto all=default_preferences();
    for (const auto& field : all.fields) if (plan.destinations.empty() || plan.destinations.contains(field)) fields.push_back(field);
    SettingSelection selected_fields;
    for (const auto& field : fields) if (preferences.fields.contains(field)) selected_fields.push_back(field);
    result.settings.push_back({"equipment","Planters",selected,0,0,labels});
    result.settings.push_back({"fields","Fields",selected_fields,0,0,fields});
    for (std::size_t i=0;i<nectar_names().size();++i) {
        extensions::ChoiceGroup group{nectar_names()[i],{}};
        for (const auto& field:fields) if (std::any_of(catalog().begin(),catalog().end(),[&](const auto& item) {
            return item.field==field && static_cast<std::size_t>(item.nectar)==i;
        })) group.choices.push_back(field);
        if (!group.choices.empty()) result.settings.back().groups.push_back(std::move(group));
    }
    SettingSelection material_labels,selected_materials;
    for (const auto& goal:item_goals()) { material_labels.push_back(goal.label); if (std::find(preferences.materials.begin(),preferences.materials.end(),goal.id)!=preferences.materials.end()) selected_materials.push_back(goal.label); }
    result.settings.push_back({"materials","Material goals",selected_materials,0,0,material_labels});
    result.settings.back().inline_choices=true;
    result.settings.back().help="Selected items include bonus routes and regular drops. Item planters grow fully. Cooldowns are estimates; manual harvests are not tracked.";
    for(const auto& goal:item_goals()) {
        auto& groups=result.settings.back().groups;
        auto group=std::find_if(groups.begin(),groups.end(),[&](const auto& g) {return g.label==goal.category;});
        if(group==groups.end()) {groups.push_back({goal.category,{}});group=std::prev(groups.end());}
        group->choices.push_back(goal.label);
    }
    const auto returns=return_choices(plan);
    if (!returns.empty()) result.settings.push_back({"return-method","Return to hive",std::string(plan.walk_return?"Walk":"Reset"),0,0,returns});
    if (has_loot(plan)) result.settings.push_back({"collect-loot","Collect loot",plan.collect_loot,0,0,{}});
    result.settings.push_back({"simulation-days","Days",7,3,30,{}});
    result.settings.push_back({"interrupt-gather","Gather interrupt",true});
    result.settings.push_back({"gather-planter-fields","Gather at planters",false});
    if (plan.conversion) {
        result.settings.push_back({"convert-after-harvest","Convert full backpack after harvest",plan.conversion->enabled,0,0,{}});
        result.settings.push_back({"conversion-threshold","Convert at backpack (%)",static_cast<int>(plan.conversion->threshold),1,100,{}});
    }
    for (std::size_t i=0;i<5;++i) {
        if (preferences.targets[i].minimum!=std::floor(preferences.targets[i].minimum) || preferences.targets[i].buffer!=std::floor(preferences.targets[i].buffer))
            throw std::invalid_argument("Module nectar defaults require whole percentages");
        result.settings.push_back({"target-"+std::to_string(i),"Target (%)",static_cast<int>(preferences.targets[i].minimum),0,100,{}});
        result.settings.push_back({"buffer-"+std::to_string(i),"Buffer (%)",static_cast<int>(preferences.targets[i].buffer),0,20,{}});
        result.settings.push_back({"priority-"+std::to_string(i),"Priority",preferences.priorities[i],0,100,{}});
    }
    result.page=extensions::PageLayout{{{"equipment","Planters",{},{"equipment"},false,1},
        {"fields","Fields",{},{"fields"},false,1}},true};
    for (std::size_t i=0;i<5;++i) { result.page->sections.push_back({"nectar-"+std::to_string(i),nectar_names()[i],{},
        {"target-"+std::to_string(i),"buffer-"+std::to_string(i),"priority-"+std::to_string(i)},false,3}); result.page->sections.back().row=true; result.page->sections.back().order_group="nectar"; result.page->sections.back().order_setting="priority-"+std::to_string(i); }
    extensions::PageSection run{"run","Harvesting",{},{"capacity","max-actions","run-seconds"},false,3};
    if (!returns.empty()) run.settings.push_back("return-method");
    if (has_loot(plan)) run.settings.push_back("collect-loot");
    run.settings.push_back("gather-planter-fields"); run.settings.push_back("interrupt-gather");
    if (plan.conversion) { run.settings.push_back("convert-after-harvest"); run.settings.push_back("conversion-threshold"); }
    result.page->sections.push_back(std::move(run));
    result.page->sections.push_back({"materials","Item goals",{},{"materials"},false,1});
    result.page->sections.back().tool="Item goals";
    result.page->sections.push_back({"material-progress","Progress","Next steps for the selected items.",{},false,1,true});
    result.page->sections.back().tool="Item goals";
    result.page->sections.push_back({"drop-reference","Drops","Full-growth rewards from the wiki. Random drops have no predicted quantity. Monthly bonuses use a conservative 36-day estimate because published cooldowns disagree. Single-use planters are consumed and stay disabled until you select them.",{},false,1});
    result.page->sections.back().tool="Drops";
    for(const auto& drops:planter_drops()) {
        const auto gear=std::find_if(equipment().begin(),equipment().end(),[&](const auto& e) {return e.id==drops.planter;});
        result.page->sections.push_back({"drops-"+std::to_string(std::distance(equipment().begin(),gear)),gear->label,drop_description(drops),{},false,1});
        result.page->sections.back().tool="Drops"; result.page->sections.back().collapsed=true;
    }
    result.page->sections.push_back({"simulation","Simulation",{},{"simulation-days"},false,1});
    result.page->sections.back().tool="Simulation";
    for(auto& section:result.page->sections) if(section.tool.empty() && !section.metrics)
        section.tab=section.id=="fields"?"Fields":"Setup";
    std::stable_partition(result.page->sections.begin(),result.page->sections.end(),[](const auto& section) { return !section.order_group.empty(); });
    const auto materials=std::find_if(result.page->sections.begin(),result.page->sections.end(),[](const auto& section) { return section.id=="materials"; });
    std::rotate(result.page->sections.begin(),materials,materials+1);
    result.page->sections.insert(result.page->sections.begin(),{"mode","Setup",{},{"mode","secondary"},false,2});
    result.page->sections.front().tab="Setup";
    for (auto& section:result.page->sections) {
        section.inline_labels=true;
        if(!section.order_group.empty()) section.panel="targets";
        if(section.id=="equipment") { section.panel="equipment"; section.columns=2; }
    }
    for(auto& setting:result.settings) {
        if(setting.id=="run-seconds" || setting.id=="max-actions" || (plan.conversion && plan.conversion->every_return && (setting.id=="convert-after-harvest" || setting.id=="conversion-threshold"))) setting.hidden=true;
        if(setting.id=="equipment" || setting.id=="fields") setting.inline_choices=true;
        if(setting.id.starts_with("priority-")) setting.hidden=true;
        if(setting.id.starts_with("priority-")) setting.help="Lower numbers are preferred when nectar needs compete; urgency still matters.";
        if(setting.id.starts_with("buffer-")) setting.help="Relative buffer: target 70 with buffer 10 maintains 63–77%.";
        if(setting.id=="return-method") setting.help="Reset respawns at your hive and loses carried pollen. Walk keeps it for conversion.";
        if(setting.id=="interrupt-gather") setting.help="Allow due planter work to end gathering early, then return safely. Off waits until gathering ends.";
    }
    return result;
}
extensions::Factory module_factory(ExecutionPlan defaults,automation::NavigationServices navigation,std::shared_ptr<Account> account,
    std::shared_ptr<const Source> source,std::shared_ptr<const Actions> actions,WallClock clock,std::function<ObservationContext()> context) {
    if (!navigation.routes || !navigation.perception) throw std::invalid_argument("Planters needs navigation services");
    auto network=std::make_shared<const game::RouteNetwork>(navigation.routes->network());
    module_manifest(defaults); planters::program(defaults,network,account,source,actions,clock);
    return [defaults=std::move(defaults),network=std::move(network),account=std::move(account),source=std::move(source),actions=std::move(actions),clock=std::move(clock),context=std::move(context)](const extensions::Manifest& m) {
        return std::make_unique<PlanterModule>(m,defaults,network,account,source,actions,clock,context);
    };
}
}
