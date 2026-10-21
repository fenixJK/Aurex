#include "module.hpp"
#include "browser.hpp"
#include <algorithm>
#include <limits>
#include "game/data/npcs.hpp"

namespace hive::features::quests {
namespace {
class QuestModule final : public extensions::Module {
    std::shared_ptr<const Source> source_;
    Tracker tracker_;
    Report report_;
    Time freshness_,since_{},last_{};
    mutable Time scan_seen_{Time(-1)};
    std::uint64_t generation_{},revision_{};
    std::string state_{"Stopped"};
    bool started_{},other_running_{};
    mutable bool scan_requested_{true};
    std::string id_;
    bool interrupt_gather_{true};
    unsigned goo_slot_{};
    mutable std::map<std::string,Time> checked_deadlines_;
    std::optional<CyclePlan> cycle_;
    mutable Counters counters_;
    std::uint64_t report_run_{};
    std::shared_ptr<CycleResults> cycle_results_{std::make_shared<CycleResults>()};
    std::optional<ObservationContext> context_;
    std::shared_ptr<const BrowserSource> browser_;
    mutable std::shared_ptr<ScanResults> scan_{std::make_shared<ScanResults>()};
public:
    QuestModule(const extensions::Manifest& manifest,std::shared_ptr<const Source> source,std::vector<Field> fields,Time freshness,int priority,std::shared_ptr<const BrowserSource> browser,Time retention,std::optional<CyclePlan> cycle)
        :source_(std::move(source)),tracker_(std::move(fields),freshness,priority,retention),freshness_(freshness),cycle_(std::move(cycle)),browser_(std::move(browser)) {
        id_=manifest.id;
        const automation::Permissions required=cycle_?automation::Permissions{automation::Permission::movement,automation::Permission::interact,automation::Permission::tool}:browser_?automation::Permissions{automation::Permission::interact}:automation::Permissions{};
        if (!source_ || manifest.permissions!=required || manifest.requests!=std::set{extensions::RequestService::fields,extensions::RequestService::enemies,extensions::RequestService::stations}
            || (cycle_?manifest.settings.size()!=3:!manifest.settings.empty()) || !manifest.scripts.empty() || !manifest.bindings.empty())
            throw std::invalid_argument("Incompatible quest module configuration");
    }
    void start(const Settings& settings) override {
        report_counters();
        stop();
        ++report_run_;
        if (cycle_) {
            if ((settings.empty() || settings.size()>3) || !settings.contains("givers") || !std::holds_alternative<SettingSelection>(settings.at("givers")))
                throw std::invalid_argument("Select the quest givers to automate");
            goo_slot_=0;
            if (const auto slot=settings.find("goo-slot");slot!=settings.end()) {
                const auto* value=std::get_if<std::string>(&slot->second);
                if (!value || (*value!="Disabled" && (value->size()!=1 || (*value)[0]<'1' || (*value)[0]>'7')))
                    throw std::invalid_argument("Choose a Gumdrop hotbar slot from 1 to 7");
                if (*value!="Disabled") goo_slot_=unsigned((*value)[0]-'0');
            }
            cycle_->givers=std::get<SettingSelection>(settings.at("givers"));
            std::set<std::string> names;
            for (const auto& name:cycle_->givers) { game::npc(name); if (!names.insert(name).second) throw std::invalid_argument("Duplicate quest giver"); }
        } else if (!settings.empty()) throw std::invalid_argument("Unexpected quest module settings");
        if(cycle_) {
            const auto it=settings.find("interrupt-gather");
            if(it!=settings.end() && !std::holds_alternative<bool>(it->second)) throw std::invalid_argument("Invalid gather interrupt setting");
            interrupt_gather_=it==settings.end() || std::get<bool>(it->second);
        }
        started_=true; state_="Waiting for a session";
    }
    std::vector<extensions::HotbarUse> hotbar_uses() const override {
        if (goo_slot_ && cycle_ && !cycle_->givers.empty()) return {{goo_slot_,"Gumdrops","Goo quests"}};
        return {};
    }
    void update(const extensions::SessionView& view) override {
        if (!started_) throw std::logic_error("Quest module is not started");
        if (view.now<Time::zero() || view.now>Time::max()-freshness_) throw std::invalid_argument("Invalid quest module time");
        if (view.status.state!=RunState::running || !view.generation) {
            tracker_.reset(); context_.reset(); report_={}; generation_=revision_=0; last_=view.now; other_running_=false; scan_requested_=true; state_="Waiting for a session"; return;
        }
        if (generation_!=view.generation) {
            tracker_.reset(); context_.reset(); report_={}; generation_=view.generation; revision_=0; since_=last_=view.now;
            scan_requested_=true; other_running_=false;
        }
        if (view.now<last_) throw std::invalid_argument("Quest module clock moved backwards");
        last_=view.now;
        if (view.feature && view.feature->module_id!=id_) {
            if (view.feature->state==automation::State::running) other_running_=true;
            else if (other_running_ && automation::can_handoff(view.feature->state)) {
                scan_requested_=scan_requested_ || (view.feature->state==automation::State::completed && view.feature->steps>0); other_running_=false;
            }
        }
        if (revision_==std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Quest request revisions exhausted");
        // The browser may scroll away before the host observes the same page.
        // Consume its completed scan directly, preserving each card's capture time.
        if (const auto completed=scan_->snapshot(scan_seen_)) {
            scan_seen_=completed->context.now;
            const auto& c=completed->context;
            if (c.generation==generation_ && c.now>=since_ && c.now<=view.now && view.now-c.now<=freshness_) {
                ObservationContext current{c.window,c.generation,view.now};
                for (const auto& card:completed->cards)
                    if (card.captured>=since_ && (!cycle_ || std::find(cycle_->givers.begin(),cycle_->givers.end(),card.value.giver)!=cycle_->givers.end()))
                        tracker_.observe(current,card);
                context_=current;
            }
        }
        const bool gathering=view.feature && view.feature->state==automation::State::running && view.feature->activity==WorkActivity::gathering;
        auto snapshot=browser_ && !gathering?source_->peek():source_->read();
        if (!snapshot || snapshot->context.generation!=generation_ || snapshot->context.now<since_
            || snapshot->context.now>view.now || view.now-snapshot->context.now>freshness_) {
            report_={};
            if (context_) { context_->now=view.now; report_=tracker_.report(*context_,true); }
            state_=(report_.requests.fields.empty() && report_.requests.enemies.empty() && report_.requests.channels.empty())?"Waiting for quest observations":"Using remembered quest objectives";
        } else {
            if (snapshot->cards.size()>16) throw std::invalid_argument("Too many quest observations");
            auto context=snapshot->context; context.now=view.now;
            context_=context;
            for (const auto& card : snapshot->cards) if (card.captured>=since_ && (!cycle_ || std::find(cycle_->givers.begin(),cycle_->givers.end(),card.value.giver)!=cycle_->givers.end())) tracker_.observe(context,card);
            report_=tracker_.report(context);
            state_=(report_.requests.fields.empty() && report_.requests.enemies.empty() && report_.requests.channels.empty())?"No active quest requests":"Contributing quest objectives";
        }
        for (const auto& [giver,objective]:report_.actions)
            if (!cycle_ || !cycle_->actions.contains(objective.target)) report_.blocked.push_back(giver+": "+objective.text+" needs an action provider");
        report_.requests.generation=generation_; report_.requests.revision=++revision_;
    }
    std::vector<extensions::Metric> metrics() const override {
        std::vector<extensions::Metric> result;
        const auto& cards=tracker_.observed_cards();
        const auto visits=cycle_results_->read();
        std::vector<std::string> givers;
        if (cycle_) givers=cycle_->givers;
        else for (const auto& [giver,_]:cards) givers.push_back(giver);
        for (const auto& giver:givers) {
            const auto visit=visits.find(giver);
            const auto found=cards.find(giver);
            const auto add=[&](std::string label,std::string value,std::optional<double> progress={}) {
                result.push_back({std::move(label),std::move(value),progress,giver});
            };
            if (found==cards.end()) add("Quest","Waiting to read");
            else {
                const auto& card=found->second.value;
                auto title=card.quest;
                if (title.starts_with(giver+": ")) title.erase(0,giver.size()+2);
                add("Quest",title);
                unsigned complete=0;
                for (const auto& row:card.objectives) complete+=row.known && row.required && row.collected==row.required;
                add("Objectives",std::to_string(complete)+(card.complete_view?" / "+std::to_string(card.objectives.size())+" complete":" complete · partial view"),
                    card.complete_view && !card.objectives.empty()?std::optional(double(complete)/card.objectives.size()):std::nullopt);
                for (const auto& row:card.objectives) {
                    auto label=row.text.empty()?"Unread objective":row.text;
                    if (row.kind==Kind::field_pollen) label=row.field+" pollen";
                    else if (row.kind==Kind::red_pollen) label="Red pollen";
                    else if (row.kind==Kind::blue_pollen) label="Blue pollen";
                    else if (row.kind==Kind::white_pollen) label="White pollen";
                    else if (row.kind==Kind::any_pollen) label="Pollen";
                    if (row.goo) {
                        const auto pollen=label.rfind("pollen");
                        if (pollen!=std::string::npos) label.replace(pollen,6,"Goo");
                    }
                    const auto value=!row.known?"Unread":row.collected==row.required?"Done":row.visual?"In progress":
                        std::to_string(row.collected)+" / "+std::to_string(row.required);
                    add(label.substr(0,160),value);
                }
                if (!card.complete_view) add("Reading","Some objectives are not visible");
                else if (last_-found->second.captured>freshness_) add("Reading","Last seen progress");
            }
            for (const auto& reason:report_.blocked) if (reason.starts_with(giver+": ")) add("Needs attention",reason.substr(giver.size()+2));
            add("This session",std::to_string(visit==visits.end()?0:visit->second.completed)+" completed");
            if (visit!=visits.end() && visit->second.next_visit>last_) {
                const auto minutes=(visit->second.next_visit-last_+Time(59999)).count()/60000;
                add("Next quest","In "+std::to_string(minutes)+" min");
            } else if (std::find(report_.ready.begin(),report_.ready.end(),giver)!=report_.ready.end()) add("Next step","Turn in quest");
        }
        if (result.empty()) result.push_back({"Quests","Choose a quest giver to see its progress"});
        return result;
    }
    std::vector<Counter> report_counters() const override {
        const auto all=cycle_results_->read();
        if (cycle_) for (const auto& giver:cycle_->givers) {
            const auto found=all.find(giver);
            counters_.observe(giver,giver,found==all.end()?0:found->second.completed,report_run_);
        }
        return counters_.values();
    }
    std::optional<std::vector<extensions::Metric>> report_metrics() const override {
        std::vector<extensions::Metric> result{{"Ready to turn in",std::to_string(report_.ready.size())}};
        if (!report_.blocked.empty()) result.push_back({"Needs attention",std::to_string(report_.blocked.size())});
        return result;
    }
    std::optional<extensions::RoutineParticipation> routine() const override { return browser_?std::optional(extensions::RoutineParticipation{80,Time{},false,true,true,interrupt_gather_}):std::nullopt; }
    bool pending_work(const extensions::SessionView& view) const override {
        return started_ && browser_ && (!cycle_ || !cycle_->givers.empty())
            && view.status.state==RunState::running && (scan_requested_ || due_visit(view.now) || pending_action());
    }
    std::optional<extensions::WorkEstimate> next_work(const extensions::SessionView& view) const override {
        if (!pending_work(view)) return {};
        if (scan_requested_) return extensions::WorkEstimate{"Read quests",{},Time{},view.now,view.now};
        return extensions::WorkEstimate{pending_action()?"Complete quest objective":"Talk to quest giver",{},Time{},view.now,{}};
    }
    bool pending_action() const {
        if (!cycle_) return false;
        for (const auto& [giver,card]:tracker_.observed_cards()) for (const auto& row:card.value.objectives) {
            if (!row.known || row.collected>=row.required) continue;
            const auto action=cycle_->actions.find(row.target);
            if (action!=cycle_->actions.end() && action->second.available && action->second.available()) return true;
        }
        return false;
    }
    bool due_visit(Time now) const {
        if (!cycle_) return false;
        for(const auto& [giver,status]:cycle_results_->read())
            if(status.next_visit>Time{} && status.next_visit<=now && (!checked_deadlines_.contains(giver) || checked_deadlines_.at(giver)<status.next_visit)) return true;
        return false;
    }
    void acknowledge_scan() const {
        scan_requested_=false;
        for(const auto& [giver,status]:cycle_results_->read()) if(status.next_visit<=last_) checked_deadlines_[giver]=status.next_visit;
    }
    bool executable() const noexcept override { return bool(browser_); }
    std::unique_ptr<automation::Program> program() const override {
        if (!browser_) return {};
        acknowledge_scan();
        scan_seen_=Time(-1); auto results=std::make_shared<ScanResults>(); auto run=cycle_?cycle_program(*cycle_,browser_,results,cycle_results_):browser_program(browser_,results,false); scan_=std::move(results); return run;
    }
    game::Requests requests() const override {
        auto requests=report_.requests;
        requests.fields.insert(requests.fields.end(),report_.completed_fields.begin(),report_.completed_fields.end());
        for (auto& request:requests.fields) request.menu=game::MenuTab::quests;
        if (goo_slot_) for (auto& request:requests.fields) {
            if (request.complete) continue;
            for (const auto& [giver,card]:tracker_.observed_cards())
                for (const auto& row:card.value.objectives)
                    if (row.goo && request.id==giver+"/"+row.id) request.hotbar_slot=goo_slot_;
        }
        return requests;
    }
    void stop() noexcept override { tracker_.reset(); cycle_results_->clear(); checked_deadlines_.clear(); context_.reset(); report_={}; generation_=revision_=0; started_=other_running_=false; scan_requested_=true; state_="Stopped"; }
};
}
extensions::Manifest module_manifest(std::string id,std::string provider,bool browser,bool cycle) {
    extensions::Manifest result;
    result.id=std::move(id); result.provider=std::move(provider); result.name="Quests"; result.version="0.1.0";
    result.enabled_by_default=true;
    result.description="Read supported quest objectives and request work without changing saved field settings.";
    result.requests={extensions::RequestService::fields,extensions::RequestService::enemies,extensions::RequestService::stations};
    if (browser) result.permissions={automation::Permission::interact};
    if (cycle) {
        result.setup_profiles={{"Honey farming",30,{{"interrupt-gather",false}}},
            {"Quest progress",10,{{"interrupt-gather",true}}},{"Materials",40,{{"interrupt-gather",false}}}};
        result.permissions.insert(automation::Permission::tool); result.permissions.insert(automation::Permission::movement); result.permissions.insert(automation::Permission::interact);
        result.settings_version=4;
        extensions::Setting givers{"givers","Quest givers",SettingSelection{"Polar Bear"}};
        for (const auto& npc:game::npcs()) givers.choices.push_back(npc.name);
        givers.ordered=true; givers.inline_choices=true; result.settings.push_back(std::move(givers));
        result.settings.push_back({"interrupt-gather","Gather interrupt",true});
        result.settings.back().help="Allow due quest work to interrupt gathering. Otherwise it waits for the current gather to finish.";
        result.settings.push_back({"goo-slot","Gumdrop slot",std::string("Disabled"),0,0,{"Disabled","1","2","3","4","5","6","7"}});
        result.settings.back().help="Put Gumdrops in this slot. Used only while gathering for an unfinished Goo objective.";
        result.page=extensions::PageLayout{{{"givers","Quest givers","",{"givers"},false,1},
            {"options","Options",{},{"interrupt-gather","goo-slot"},false,2},
            {"objectives","Quest progress",{},{},true,2}},true};
    } else result.page=extensions::PageLayout{{{"objectives","Quest progress",{},{},true,2}}};
    for (auto& section:result.page->sections) section.inline_labels=true;
    return result;
}
extensions::Factory module_factory(std::shared_ptr<const Source> source,std::vector<Field> fields,Time freshness,int priority,std::shared_ptr<const BrowserSource> browser,Time retention,std::optional<CyclePlan> cycle) {
    if (!source) throw std::invalid_argument("Quests needs an observation source");
    Tracker validation(fields,freshness,priority,retention);
    return [source=std::move(source),fields=std::move(fields),freshness,priority,browser=std::move(browser),retention,cycle=std::move(cycle)](const extensions::Manifest& manifest) {
        return std::make_unique<QuestModule>(manifest,source,fields,freshness,priority,browser,retention,cycle);
    };
}
}
