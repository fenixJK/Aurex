#include "visit_module.hpp"
#include <algorithm>
#include <tuple>

namespace hive::features::visits {
automation::ProgramFactory station_request(automation::VisitPlan plan,const std::string& id,std::shared_ptr<const game::RouteNetwork> network,
    std::shared_ptr<const automation::AvailabilitySource> availability,std::shared_ptr<visits::Cooldowns> saved) {
    std::erase_if(plan.visits,[&](const auto& station) { return station.id!=id; });
    if (plan.visits.empty()) return {};
    if (saved) availability=features::visits::saved_availability(std::move(availability),saved,plan);
    return [plan=std::move(plan),network=std::move(network),availability=std::move(availability),saved=std::move(saved)] {
        auto run=automation::visit_program(plan,network,availability);
        return saved?features::visits::save_cooldowns(std::move(run),saved):std::move(run.program);
    };
}

std::string selection_key(std::string_view identity) {
    std::string key="target";
    bool separator=true;
    for (unsigned char c : identity) {
        if (c>=128 || c<32 || c==127) throw std::invalid_argument("Invalid activity setting identity");
        if (c>='A' && c<='Z') c+= 'a'-'A';
        if ((c>='a' && c<='z') || (c>='0' && c<='9')) {
            if (separator) key+='-';
            key+=char(c); separator=false;
        } else separator=true;
    }
    if (key=="target" || key.size()>80) throw std::invalid_argument("Invalid activity setting identity");
    return key;
}
AvailableSettings::AvailableSettings(const automation::VisitPlan& catalog,const automation::VisitPlan& available) {
    for (const auto& visit:catalog.visits)
        if (std::none_of(available.visits.begin(),available.visits.end(),[&](const auto& item) { return item.id==visit.id; }))
            unavailable_.insert(selection_key(visit.id));
}
extensions::Manifest AvailableSettings::manifest(extensions::Manifest value) const {
    for (const auto& key:unavailable_) {
        const auto found=std::find_if(value.settings.begin(),value.settings.end(),[&](const auto& setting) { return setting.id==key; });
        if (found==value.settings.end() || !std::holds_alternative<bool>(found->initial) || !found->choices.empty()
            || found->minimum!=0 || found->maximum!=0) throw std::invalid_argument("Invalid activity selection schema");
        value.settings.erase(found);
    }
    value.page.reset();
    return value;
}
Settings AvailableSettings::values(Settings value) const {
    for (const auto& key:unavailable_) {
        const auto found=value.find(key);
        if (found==value.end() || !std::holds_alternative<bool>(found->second)) throw std::invalid_argument("Missing activity selection");
        value.erase(found);
    }
    return value;
}
Settings migrate_selections(const Settings& values,const std::vector<std::string>& previous) {
    Settings result;
    std::map<std::string,std::string> keys;
    for (std::size_t i=0;i<previous.size();++i) keys.emplace("target-"+std::to_string(i),selection_key(previous[i]));
    for (const auto& [key,value] : values) {
        const auto found=keys.find(key);
        if (key.starts_with("target-") && found==keys.end()) throw std::invalid_argument("Unknown previous activity selection");
        if (found!=keys.end() && !std::holds_alternative<bool>(value)) throw std::invalid_argument("Invalid previous activity selection");
        if (!result.emplace(found==keys.end()?key:found->second,value).second) throw std::invalid_argument("Conflicting previous activity selections");
    }
    return result;
}
namespace {
std::vector<extensions::Setting> settings(const automation::VisitPlan& plan) {
    if (plan.visits.size()>32u || plan.config.duration<Time(1000) || plan.config.duration>std::chrono::hours(8) || plan.config.duration.count()%1000)
        throw std::invalid_argument("Invalid activity run limit");
    std::vector<extensions::Setting> result{{"run-seconds","Run limit (seconds)",static_cast<int>(plan.config.duration.count()/1000),1,28800,{}}};
    std::set<std::string> keys;
    for (const auto& visit : plan.visits) {
        const auto key=selection_key(visit.id);
        if (!keys.insert(key).second) throw std::invalid_argument("Conflicting activity setting identities");
        result.push_back({key,visit.id,true});
    }
    result.push_back({"interrupt-gather","Gather interrupt",false});
    return result;
}
std::string status(automation::VisitStage stage) {
    using enum automation::VisitStage;
    switch (stage) {
    case pending: return "Pending"; case deferred: return "Deferred"; case unavailable: return "Not available";
    case preparing: return "Preparing travel";
    case travelling: return "Travelling"; case working: return "Working"; case awaiting_loot: return "Waiting for loot sweep"; case looting: return "Collecting loot";
    case settling: return "Waiting for rewards";
    case completed: return "Confirmed"; case interrupted: return "Interrupted";
    }
    throw std::logic_error("Invalid visit stage");
}
class RecheckSource final : public automation::AvailabilitySource {
    std::shared_ptr<const automation::AvailabilitySource> source_;
    std::map<std::string,Time> ready_;
    std::uint64_t generation_{};
    Window window_;
    bool cooling(const std::string& id,const automation::View& view) const {
        const auto it=ready_.find(id);
        const auto& w=view.context.window;
        return generation_==view.context.generation && window_.id==w.id && window_.process==w.process && window_.bounds==w.bounds
            && it!=ready_.end() && view.context.now<it->second;
    }
public:
    RecheckSource(std::shared_ptr<const automation::AvailabilitySource> source,std::map<std::string,Time> ready,std::uint64_t generation,Window window)
        :source_(std::move(source)),ready_(std::move(ready)),generation_(generation),window_(std::move(window)) {}
    std::vector<Rect> text_regions(const std::string& id) const override { return source_->text_regions(id); }
    Observation<bool> read(const std::string& id,const automation::View& view) const override {
        if (cooling(id,view))
            return {Knowledge::present,false,view.context.now,view.context.window,view.context.generation};
        return source_->read(id,view);
    }
    Observation<Time> remaining(const std::string& id,const automation::View& view) const override {
        return cooling(id,view)?Observation<Time>{}:source_->remaining(id,view);
    }
};
class Selection final : public automation::AvailabilitySource {
    std::shared_ptr<const automation::AvailabilitySource> source_;
    std::set<std::string> selected_;
    Demand demand_;
    bool selected(const std::string& id,const automation::View& view) const {
        return selected_.contains(id) || (demand_ && demand_(view.context).contains(id));
    }
public:
    Selection(std::shared_ptr<const automation::AvailabilitySource> source,std::set<std::string> selected,Demand demand)
        :source_(std::move(source)),selected_(std::move(selected)),demand_(std::move(demand)) {}
    std::vector<Rect> text_regions(const std::string& id) const override { return source_->text_regions(id); }
    Observation<bool> read(const std::string& id,const automation::View& view) const override {
        return selected(id,view)?source_->read(id,view):Observation<bool>{Knowledge::present,false,view.context.now,view.context.window,view.context.generation};
    }
    Observation<Time> remaining(const std::string& id,const automation::View& view) const override {
        return selected(id,view)?source_->remaining(id,view):Observation<Time>{};
    }
};
class VisitModule final : public extensions::Module {
    std::string id_;
    automation::VisitPlan defaults_,plan_;
    std::shared_ptr<const game::RouteNetwork> network_;
    std::shared_ptr<const automation::AvailabilitySource> availability_;
    mutable std::shared_ptr<const automation::VisitResults> results_;
    mutable Counters counters_;
    mutable std::uint64_t report_run_{};
    std::string detail_;
    bool started_{},interrupt_{};
    Demand demand_;
    std::shared_ptr<Cooldowns> saved_;
    std::optional<ObservationContext> demand_context_;
    std::set<std::string> selected_;
    mutable std::map<std::string,Time> ready_;
    mutable std::uint64_t generation_{};
    mutable Window window_;
    mutable std::uint64_t accounted_run_{};
    mutable std::map<std::string,Time> retry_after_;
    std::map<std::string,Time> travel_costs_;
    mutable std::string planned_visit_;
    Time ready_at(const automation::Visit& visit,const ObservationContext& context) const {
        auto ready=context.now;
        if (saved_) ready+=std::max(Time{},saved_->remaining(visit.id,visit.cooldown));
        else if (generation_==context.generation) if (const auto found=ready_.find(visit.id);found!=ready_.end()) ready=std::max(ready,found->second);
        if (generation_==context.generation) if (const auto found=retry_after_.find(visit.id);found!=retry_after_.end()) ready=std::max(ready,found->second);
        return ready;
    }
    void remember_run(Time now=Time(-1)) const {
        if (!results_) return;
        const auto report=results_->read();
        if (report.state==automation::State::ready || report.state==automation::State::running || report.state==automation::State::paused
            || accounted_run_==report_run_) return;
        if (now>=Time{}) accounted_run_=report_run_;
        if (report.generation!=generation_ || report.window.id!=window_.id || report.window.process!=window_.process) {
            ready_.clear(); retry_after_.clear(); generation_=report.generation; window_=report.window;
        }
        const auto requested=now>=Time{} && demand_?demand_({report.window,report.generation,now}):RequestedVisits{};
        for (const auto& visit:report.visits) {
            if (visit.observed_ready_at) ready_[visit.id]=*visit.observed_ready_at;
            else if (visit.confirmed_at && visit.estimated_ready_at) ready_[visit.id]=*visit.estimated_ready_at;
            if (now<Time{} || report.state==automation::State::stopped || visit.confirmed_at || visit.observed_ready_at) continue;
            if (demand_ && !selected_.contains(visit.id) && !requested.contains(visit.id)) continue;
            if (visit.stage!=automation::VisitStage::deferred && visit.stage!=automation::VisitStage::interrupted
                && visit.stage!=automation::VisitStage::unavailable) continue;
            const auto target=std::find_if(plan_.visits.begin(),plan_.visits.end(),[&](const auto& item) { return item.id==visit.id; });
            if (target==plan_.visits.end() || (target->scheduled && !target->scheduled({report.window,report.generation,now}))) continue;
            const auto ready=ready_.find(visit.id);
            if ((ready!=ready_.end() && now<ready->second) || (saved_ && saved_->remaining(visit.id,target->cooldown)>Time{})) continue;
            auto& retry=retry_after_[visit.id];
            if (retry<=now) retry=now+std::chrono::minutes(5);
        }
    }
public:
    VisitModule(const extensions::Manifest& m,automation::VisitPlan plan,std::shared_ptr<const game::RouteNetwork> network,
        std::shared_ptr<const automation::AvailabilitySource> availability,const automation::Permissions& permissions,Demand demand,std::shared_ptr<Cooldowns> saved)
        :id_(m.id),defaults_(std::move(plan)),network_(std::move(network)),availability_(std::move(availability)),demand_(std::move(demand)),saved_(std::move(saved)) {
        const auto schema=settings(defaults_);
        if (id_.empty() || m.permissions!=permissions || !m.requests.empty() || !m.scripts.empty() || !m.sources.empty()
            || !m.bindings.empty() || m.settings.size()!=schema.size()) throw std::invalid_argument("Incompatible activity module manifest");
        std::set<std::string> seen;
        for (const auto& s : m.settings) {
            const auto it=std::find_if(schema.begin(),schema.end(),[&](const auto& e) { return e.id==s.id; });
            if (it==schema.end() || !seen.insert(s.id).second || s.initial.index()!=it->initial.index() || !s.choices.empty()
                || s.minimum!=it->minimum || s.maximum!=it->maximum) throw std::invalid_argument("Incompatible activity settings schema");
            extensions::validate_setting(s,s.initial);
        }
        if (network_->contains("hive.proximity")) {
            std::set<std::string> access;
            for (const auto& route:network_->routes()) {
                access.insert(route.requirements.begin(),route.requirements.end());
                access.insert(route.departure_requirements.begin(),route.departure_requirements.end());
            }
            const auto paths=network_->paths_from("hive.proximity",access);
            for (const auto& visit:defaults_.visits) if (const auto found=paths.find(visit.destination);found!=paths.end()) {
                auto duration=found->second.estimate;
                if (defaults_.finish_at) if (const auto back=network_->plan(visit.destination,*defaults_.finish_at,access)) duration+=back->estimate;
                travel_costs_[visit.id]=duration;
            }
        }
    }
    void start(const Settings& values) override {
        extensions::validate_settings(settings(defaults_),values);
        auto plan=defaults_;
        plan.config.duration=Time(std::get<int>(values.at("run-seconds"))*1000);
        std::set<std::string> selected;
        for (const auto& visit : plan.visits)
            if (std::get<bool>(values.at(selection_key(visit.id)))) selected.insert(visit.id);
        if (!demand_) std::erase_if(plan.visits,[&](const auto& visit) { return !selected.contains(visit.id); });
        if (plan.visits.empty()) throw std::invalid_argument("Select at least one activity");
        automation::visit_program(plan,network_,availability_);
        if (saved_) saved_->load();
        selected_=std::move(selected); interrupt_=std::get<bool>(values.at("interrupt-gather"));
        plan_=std::move(plan); results_.reset(); ready_.clear(); retry_after_.clear(); generation_=0; demand_context_.reset(); detail_.clear(); started_=true;
    }
    void update(const extensions::SessionView& view) override {
        if (!started_) throw std::logic_error("Activity module is stopped");
        detail_=view.feature && view.feature->module_id==id_?view.feature->message:std::string{};
        remember_run(view.now);
        demand_context_=view.status.state==RunState::running && view.generation?std::optional(ObservationContext{{},view.generation,view.now}):std::nullopt;
    }
    std::vector<extensions::Metric> metrics() const override {
        if (!started_) return {{"Status","Stopped"}};
        if (!results_) return {{"Status","Ready"},{"Selected",std::to_string(selected_.size())}};
        const auto report=results_->read();
        const auto state=report.state;
        std::string label=state==automation::State::ready?"Ready":state==automation::State::running?"Running":state==automation::State::completed?"Finished":
            state==automation::State::deferred?"Deferred until the next check":state==automation::State::paused?"Paused":state==automation::State::failed?"Stopped after an error":"Stopped";
        std::vector<extensions::Metric> result{{"Status",label}};
        unsigned confirmed=0,deferred=0,unavailable=0;
        for (const auto& visit : report.visits) {
            confirmed+=visit.confirmed_at.has_value(); deferred+=visit.stage==automation::VisitStage::deferred;
            unavailable+=visit.stage==automation::VisitStage::unavailable;
        }
        result.push_back({"Confirmed",std::to_string(confirmed)+" / "+std::to_string(report.visits.size())});
        result.push_back({"Deferred",std::to_string(deferred)}); result.push_back({"Unavailable",std::to_string(unavailable)});
        for (const auto& visit : report.visits) {
            if (result.size()>=12) break;
            result.push_back({visit.id,status(visit.stage)});
        }
        if (!detail_.empty()) result.push_back({"Current action",detail_});
        return result;
    }
    std::vector<Counter> report_counters() const override {
        const auto report=results_?results_->read():automation::VisitReport{};
        for (const auto& visit:plan_.visits) {
            const auto found=std::find_if(report.visits.begin(),report.visits.end(),[&](const auto& result) { return result.id==visit.id; });
            counters_.observe(visit.id,visit.id,found!=report.visits.end() && found->confirmed_at.has_value()?1:0,report_run_);
        }
        return counters_.values();
    }
    std::vector<extensions::ModuleTimer> timers() const override {
        const auto records=saved_?saved_->records():game::CooldownRecords{};
        const auto requested=demand_ && demand_context_?demand_(*demand_context_):RequestedVisits{};
        std::vector<extensions::ModuleTimer> result;
        for (const auto& visit:plan_.visits) if (selected_.contains(visit.id) || requested.contains(visit.id)) {
            const auto found=records.find(visit.id);
            auto remaining=found==records.end()?std::optional<Time>{}:saved_->remaining(visit.id,visit.cooldown);
            std::string detail=found!=records.end() && found->second.estimated?"Estimated cooldown":"";
            if (demand_context_) {
                const auto wait=ready_at(visit,*demand_context_)-demand_context_->now;
                if (wait>Time{}) {
                    if (generation_==demand_context_->generation)
                        if (const auto retry=retry_after_.find(visit.id);retry!=retry_after_.end() && retry->second==demand_context_->now+wait) detail="Retry delay";
                    remaining=wait;
                }
            }
            if (requested.contains(visit.id)) detail=detail.empty()?"Requested task":"Requested task · "+detail;
            result.push_back({visit.id,visit.id,std::move(detail),remaining});
        }
        return result;
    }
    std::optional<std::vector<extensions::Metric>> report_metrics() const override {
        return std::vector<extensions::Metric>{{"Status",metrics().front().value},{"Selected",std::to_string(selected_.size())}};
    }
    void stop() noexcept override { started_=false; results_.reset(); demand_context_.reset(); detail_.clear(); }
    std::optional<extensions::RoutineParticipation> routine() const override { return extensions::RoutineParticipation{40,Time{},false,true,true,interrupt_}; }
    bool pending_work(const extensions::SessionView& view) const override {
        const auto work=next_work(view); return work && work->ready<=view.now;
    }
    bool executable() const noexcept override { return true; }
    std::optional<extensions::WorkEstimate> next_work(const extensions::SessionView& view) const override {
        if (!started_) return {};
        remember_run(view.now);
        if (generation_ && generation_!=view.generation) { ready_.clear(); retry_after_.clear(); }
        const auto requested=demand_?demand_({{},view.generation,view.now}):RequestedVisits{};
        std::optional<extensions::WorkEstimate> next;
        for (const auto& visit:plan_.visits) {
            if (demand_ && !selected_.contains(visit.id) && !requested.contains(visit.id)) continue;
            if (visit.scheduled && !visit.scheduled({{},view.generation,view.now})) continue;
            const auto ready=ready_at(visit,{{},view.generation,view.now});
            Time duration=visit.settle+visit.loot_duration;
            if (const auto found=travel_costs_.find(visit.id);found!=travel_costs_.end()) duration+=found->second;
            if (const auto* interaction=std::get_if<automation::UseStation>(&visit.action)) duration+=interaction->timeout;
            else duration+=std::get<automation::Pattern>(visit.action).duration;
            if (!visit.group.empty()) for (const auto& member:plan_.visits)
                if (member.id!=visit.id && member.group==visit.group) duration+=std::get<automation::Pattern>(member.action).duration;
            extensions::WorkEstimate work{visit.id,visit.destination,duration,ready,{}};
            if (const auto request=requested.find(visit.id);request!=requested.end()) work.requested_by=request->second;
            if (visit.scheduled) work.finish_by=ready+duration;
            const auto order=[](const auto& work) { return std::tuple(work.ready,work.requested_by.empty(),work.duration,work.label); };
            if (!next || order(work)<order(*next)) next=std::move(work);
        }
        planned_visit_=next?next->label:std::string{};
        return next;
    }
    std::unique_ptr<automation::Program> program() const override {
        if (!started_) throw std::logic_error("Enable the activity module before starting it");
        remember_run();
        std::shared_ptr<const automation::AvailabilitySource> source=availability_;
        if (saved_) source=saved_availability(source,saved_,plan_);
        if (demand_) source=std::make_shared<Selection>(source,selected_,demand_);
        auto delayed=saved_?std::map<std::string,Time>{}:ready_;
        for (const auto& [id,time]:retry_after_) delayed[id]=std::max(delayed[id],time);
        if (!delayed.empty()) source=std::make_shared<RecheckSource>(source,std::move(delayed),generation_,window_);
        auto plan=plan_;
        auto first=std::find_if(plan.visits.begin(),plan.visits.end(),[&](const auto& visit) { return visit.id==planned_visit_; });
        if (first!=plan.visits.end()) {
            auto last=first+1;
            if (!first->group.empty()) {
                const auto group=first->group;
                while (first!=plan.visits.begin() && (first-1)->group==group) --first;
                while (last!=plan.visits.end() && last->group==group) ++last;
            }
            std::rotate(plan.visits.begin(),first,last);
        }
        auto run=automation::visit_program(std::move(plan),network_,std::move(source));
        report_counters(); ++report_run_; results_=run.results;
        return saved_?save_cooldowns(std::move(run),saved_):std::move(run.program);
    }
};
}
extensions::Manifest manifest(const automation::VisitPlan& plan,std::string id,std::string provider,std::string name,automation::Permissions permissions) {
    extensions::Manifest result;
    result.id=std::move(id); result.provider=std::move(provider); result.name=std::move(name); result.version="0.1.0";
    result.settings_version=2;
    result.description="Visit selected destinations and confirm each outcome.";
    result.permissions=std::move(permissions); result.settings=settings(plan);
    result.page=extensions::PageLayout{{{"overview","Progress",{},{},true,3}}};
    extensions::PageSection targets{"targets","Targets"};
    for (const auto& visit : plan.visits) targets.settings.push_back(selection_key(visit.id));
    targets.columns=2;
    if (!targets.settings.empty()) result.page->sections.push_back(std::move(targets));
    result.page->sections.push_back({"run","Run limit",{},{"run-seconds"},false,1});
    result.settings.front().hidden=true;
    result.page->sections.push_back({"schedule","Schedule",{},{"interrupt-gather"},false,1});
    return result;
}
extensions::Factory factory(automation::VisitPlan defaults,automation::NavigationServices navigation,
    std::shared_ptr<const automation::AvailabilitySource> availability,automation::Permissions permissions,Demand demand,std::shared_ptr<Cooldowns> saved) {
    if (!navigation.routes || !navigation.perception || !availability) throw std::invalid_argument("Activity module needs navigation and availability observations");
    auto network=std::make_shared<const game::RouteNetwork>(navigation.routes->network());
    settings(defaults); automation::visit_program(defaults,network,availability);
    return [defaults=std::move(defaults),network=std::move(network),availability=std::move(availability),permissions=std::move(permissions),demand=std::move(demand),saved=std::move(saved)](const extensions::Manifest& m) {
        return std::make_unique<VisitModule>(m,defaults,network,availability,permissions,demand,saved);
    };
}
}
