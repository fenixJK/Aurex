#include "hive/modules/planters/module.hpp"
#include <algorithm>
#include <future>
#include "hive/modules/planters/presentation.hpp"

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
    mutable std::shared_ptr<const ExecutionResults> results_;
    mutable Counters counters_;
    mutable std::uint64_t report_run_{};
    bool started_{},gather_fields_{},other_running_{},interrupt_gather_{true};
    mutable bool check_requested_{true};
    mutable std::uint64_t timer_revision_{};
    std::uint64_t generation_{};
    std::string id_;
    game::Requests requests_;
    unsigned simulation_days_{7};
    std::shared_ptr<std::atomic_bool> cancel_preview_{std::make_shared<std::atomic_bool>(false)};
    std::future<std::shared_ptr<const extensions::Plot>> preview_;
    std::shared_ptr<const extensions::Plot> plot_;
    std::string preview_status_;
public:
    PlanterModule(const extensions::Manifest& manifest,ExecutionPlan defaults,std::shared_ptr<const game::RouteNetwork> network,
        std::shared_ptr<Account> account,std::shared_ptr<const Source> source,std::shared_ptr<const Actions> actions,WallClock clock)
        :defaults_(std::move(defaults)),network_(std::move(network)),account_(std::move(account)),source_(std::move(source)),actions_(std::move(actions)),clock_(std::move(clock)),id_(manifest.id) {
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
        auto values=saved; values.try_emplace("interrupt-gather",true);
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
        const auto selection=[&](const char* id) -> const SettingSelection& {
            const auto it=values.find(id);
            if (it==values.end() || !std::holds_alternative<SettingSelection>(it->second)) throw std::invalid_argument("Missing planter selection");
            const auto& items=std::get<SettingSelection>(it->second);
            if (items.size()>20 || std::set<std::string>(items.begin(),items.end()).size()!=items.size()) throw std::invalid_argument("Invalid planter selection");
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
        validate(preferences); plan.preferences=std::move(preferences);
        planters::program(plan,network_,account_,source_,actions_,clock_); plan_=std::move(plan); results_.reset(); started_=true;
    }
    void update(const extensions::SessionView& view) override {
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
        return {{"simulate","Run","Simulation"},std::move(timers),std::move(check)};
    }
    extensions::CommandResult execute_command(const std::string& id,const extensions::CommandInput& input) override {
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
    void invoke(const std::string& command) override {
        if(command!="simulate" || !started_ || !plan_.preferences) throw std::invalid_argument("Simulation is unavailable");
        if(preview_.valid() && preview_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
        cancel_preview_=std::make_shared<std::atomic_bool>(false); preview_status_="Running";
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
        if (plan_.preferences) for (std::size_t i=0;i<5;++i) {
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
        const auto plants=account_->plants();
        if (plants.empty()) return false;
        const auto now=clock_();
        return std::any_of(plants.begin(),plants.end(),[&](const auto& plant) { return ready_at(plant)<=now; });
    }
    bool executable() const noexcept override { return true; }
    std::unique_ptr<automation::Program> program() const override {
        if (!started_) throw std::logic_error("Enable Planters before starting it");
        check_requested_=false;
        timer_revision_=account_->edit_revision();
        auto run=planters::program(plan_,network_,account_,source_,actions_,clock_);
        report_counters(); ++report_run_; results_=run.results; return std::move(run.program);
    }
};
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
    result.settings_version=2;
    result.description="Build and maintain nectar with observed placement, growth and harvest outcomes."; result.permissions=permissions;
    result.settings={{"run-seconds","Run limit (seconds)",static_cast<int>(plan.config.duration.count()/1000),1,28800,{}},
        {"capacity","Slots",static_cast<int>(plan.capacity),1,3,{}},{"max-actions","Actions per run",static_cast<int>(plan.max_actions),1,32,{}}};
    const auto preferences=plan.preferences.value_or(default_preferences()); validate(preferences);
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
        {"fields","Fields",{},{"fields"},false,1}}};
    for (std::size_t i=0;i<5;++i) { result.page->sections.push_back({"nectar-"+std::to_string(i),nectar_names()[i],{},
        {"target-"+std::to_string(i),"buffer-"+std::to_string(i),"priority-"+std::to_string(i)},false,3}); result.page->sections.back().row=true; result.page->sections.back().order_group="nectar"; result.page->sections.back().order_setting="priority-"+std::to_string(i); }
    extensions::PageSection run{"run","Settings",{},{"capacity","max-actions","run-seconds"},false,3};
    if (!returns.empty()) run.settings.push_back("return-method");
    if (has_loot(plan)) run.settings.push_back("collect-loot");
    run.settings.push_back("gather-planter-fields"); run.settings.push_back("interrupt-gather");
    if (plan.conversion) { run.settings.push_back("convert-after-harvest"); run.settings.push_back("conversion-threshold"); }
    result.page->sections.push_back(std::move(run));
    result.page->sections.push_back({"simulation","Simulation",{},{"simulation-days"},false,1});
    result.page->sections.back().tool="Simulation";
    for(auto& section:result.page->sections) if(section.tool.empty() && !section.metrics)
        section.tab=section.id=="equipment"?"Planters":section.id=="fields"?"Fields":"Nectar";
    std::stable_partition(result.page->sections.begin(),result.page->sections.end(),[](const auto& section) { return section.tab=="Nectar"; });
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
    std::shared_ptr<const Source> source,std::shared_ptr<const Actions> actions,WallClock clock) {
    if (!navigation.routes || !navigation.perception) throw std::invalid_argument("Planters needs navigation services");
    auto network=std::make_shared<const game::RouteNetwork>(navigation.routes->network());
    module_manifest(defaults); planters::program(defaults,network,account,source,actions,clock);
    return [defaults=std::move(defaults),network=std::move(network),account=std::move(account),source=std::move(source),actions=std::move(actions),clock=std::move(clock)](const extensions::Manifest& m) {
        return std::make_unique<PlanterModule>(m,defaults,network,account,source,actions,clock);
    };
}
}
