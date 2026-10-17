#include "visits.hpp"
#include <utility>
#include "game/data/stations.hpp"
#include <algorithm>

namespace hive::automation {
namespace {
class StationAvailability final : public AvailabilitySource {
    std::map<std::string,std::string> stations_;
public:
    explicit StationAvailability(std::map<std::string,std::string> stations):stations_(std::move(stations)) {
        if (stations_.empty() || stations_.size()>32) throw std::invalid_argument("Invalid station catalog");
        for (const auto& [id,name] : stations_) {
            if (id.empty() || id.size()>80 || std::any_of(id.begin(),id.end(),[](unsigned char c) { return c<32 || c==127; }))
                throw std::invalid_argument("Invalid station identity");
            game::station_target(name);
            game::station_target(name,true);
        }
    }
    std::vector<Rect> text_regions(const std::string& id) const override {
        const auto& name=stations_.at(id);
        const auto* definition=game::station_definition(name);
        if (definition && definition->interaction==game::StationInteraction::activate) return {};
        return {game::station_target(name).region};
    }
    Observation<bool> read(const std::string& id,const View& view) const override {
        const auto it=stations_.find(id);
        if (it==stations_.end() || !view.observations) return {};
        const auto& name=it->second;
        const auto* definition=game::station_definition(name);
        if (definition && definition->interaction==game::StationInteraction::activate) {
            // A generic banner identifies readiness only at the destination of the completed route.
            if (!view.route_position || !view.route_position->usable(view.context)
                || view.route_position->location!=definition->location) return {};
            const auto& banner=view.observations->interaction_banner;
            if (!banner.has_value(view.context,Time(2000)) || banner.value==game::InteractionBanner::none) return {};
            return {Knowledge::present,banner.value==game::InteractionBanner::ready,banner.captured,banner.window,banner.generation};
        }
        const auto cooldown=game::read_text_target(view.context,*view.observations,game::station_target(name,true));
        if (cooldown.has_value(view.context,Time(2000)))
            return {Knowledge::present,false,cooldown.captured,cooldown.window,cooldown.generation};
        const auto ready=game::read_text_target(view.context,*view.observations,game::station_target(name));
        if (cooldown.knowledge!=Knowledge::absent || !ready.has_value(view.context,Time(2000)) || !ready.value) return {};
        return ready;
    }
    Observation<Time> remaining(const std::string& id,const View& view) const override {
        const auto it=stations_.find(id);
        if (it==stations_.end() || !view.observations) return {};
        return game::read_text_cooldown(view.context,*view.observations,game::station_target(it->second,true));
    }
};
}
std::shared_ptr<const AvailabilitySource> station_availability(std::map<std::string,std::string> stations) {
    return std::make_shared<StationAvailability>(std::move(stations));
}
VisitReport VisitResults::read() const { std::lock_guard lock(mutex_); return report_; }
class VisitProgram final : public Program {
    VisitPlan plan_;
    std::shared_ptr<const AvailabilitySource> availability_;
    std::shared_ptr<VisitResults> results_;
    std::size_t index_{},loot_{},issued_{},acknowledged_{};
    enum class Phase { select, preparing, arriving, action, settling, loot, departing, returning, done } phase_{Phase::select};
    Time last_{},settle_until_{},loot_started_{};
    bool travelled_{};
    bool prepared_{};
    bool yield_{};
    std::optional<Time> availability_since_;
    std::optional<Time> arrived_;
    Time departed_{};
    struct ApproachSegment { std::string route; std::size_t checkpoint; Time entered; };
    struct Evidence {
        Time last_outcome{Time(-1)};
        std::optional<Time> scoped,seen_alive,clear_outcome;
        std::optional<ApproachSegment> approach_segment;
        bool confirmed{};
    };
    std::vector<Evidence> evidence_;
    std::size_t group_begin_{},group_end_{};
    std::optional<std::size_t> group_loot_;
    enum class Availability { ready, waiting, skip };
    void stage(VisitStage stage) {
        std::lock_guard lock(results_->mutex_); results_->report_.visits.at(index_).stage=stage;
    }
    void remember_timer(const View& view) {
        {
            std::lock_guard lock(results_->mutex_);
            if (results_->report_.visits.at(index_).observed_ready_at) return;
        }
        const auto remaining=availability_->remaining(plan_.visits[index_].id,view);
        if (remaining.has_value(view.context,Time(2000),arrived_.value_or(Time{}))
            && remaining.value>Time::zero() && remaining.value<=plan_.visits[index_].cooldown) {
            std::lock_guard lock(results_->mutex_);
            results_->report_.visits.at(index_).observed_ready_at=remaining.captured+remaining.value;
        }
    }
    Availability available(const View& view) {
        const auto reading=availability_->read(plan_.visits[index_].id,view);
        const auto since=arrived_ && std::holds_alternative<UseStation>(plan_.visits[index_].action)?departed_:arrived_.value_or(Time{});
        const bool known=reading.has_value(view.context,Time(2000),since);
        if (known && !reading.value) {
            stage(VisitStage::unavailable);
            remember_timer(view);
            availability_since_.reset(); return Availability::skip;
        }
        if (const auto& scheduled=plan_.visits[index_].scheduled; scheduled && !scheduled(view.context)) { stage(VisitStage::unavailable); return Availability::skip; }
        if (!known) {
            stage(VisitStage::deferred);
            if (!availability_since_) availability_since_=view.context.now;
            if (view.context.now-*availability_since_<Time(3000)) return Availability::waiting;
            availability_since_.reset(); return Availability::skip;
        }
        availability_since_.reset();
        return Availability::ready;
    }
    Step emit(Step step) { ++issued_; return step; }
    void confirm(std::size_t index,Time captured) {
        if (captured<Time::zero() || captured>Time::max()-plan_.visits[index].cooldown)
            throw std::invalid_argument("Invalid activity confirmation time");
        evidence_[index].confirmed=true;
        std::lock_guard lock(results_->mutex_); auto& result=results_->report_.visits.at(index);
        result.confirmed_at=captured; result.estimated_ready_at=captured+plan_.visits[index].cooldown;
        if (index!=index_) result.stage=VisitStage::working;
        else if (phase_==Phase::action) result.stage=plan_.visits[index].loot.empty()?VisitStage::completed:VisitStage::awaiting_loot;
    }
    void advance() {
        ++index_; arrived_.reset(); availability_since_.reset(); phase_=Phase::select;
        group_loot_.reset();
    }
    Step leave(const View& view) {
        if (plan_.visits[index_].departure) {
            phase_=Phase::departing; prepared_=false;
            return emit(RunProgram{plan_.visits[index_].departure});
        }
        advance(); return select(view);
    }
    Step finish_member(const View& view) {
        if (evidence_[index_].confirmed && plan_.visits[index_].settle>Time{} && phase_!=Phase::settling) {
            phase_=Phase::settling; stage(VisitStage::settling);
            settle_until_=view.context.now+plan_.visits[index_].settle;
        }
        if (phase_==Phase::settling && view.context.now<settle_until_)
            return emit(Wait{std::min(Time(1000),settle_until_-view.context.now),"Waiting for rewards"});
        if (evidence_[index_].confirmed) stage(plan_.visits[index_].loot.empty()?VisitStage::completed:VisitStage::awaiting_loot);
        if (evidence_[index_].confirmed && !plan_.visits[index_].loot.empty() && !group_loot_) group_loot_=index_;
        if (index_+1<group_end_) {
            ++index_; availability_since_.reset(); phase_=Phase::arriving;
            return next(view);
        }
        if (group_loot_) {
            phase_=Phase::loot; loot_=0; loot_started_=view.context.now;
            std::lock_guard lock(results_->mutex_);
            for (auto i=group_begin_;i<group_end_;++i)
                if (evidence_[i].confirmed) results_->report_.visits[i].stage=VisitStage::looting;
        } else return leave(view);
        return next(view);
    }
    Step select(const View& view) {
        while (!yield_ && index_<plan_.visits.size()) {
            const auto readiness=available(view);
            const bool inspect=readiness==Availability::waiting && plan_.visits[index_].inspect_when_unknown;
            if (readiness==Availability::waiting && !inspect) return emit(Wait{Time(100),"Reading activity availability"});
            if (readiness==Availability::ready || inspect) {
                if (plan_.setup && !prepared_) {
                    prepared_=true; phase_=Phase::preparing; stage(VisitStage::preparing);
                    return emit(RunProgram{plan_.setup});
                }
                arrived_.reset(); availability_since_.reset();
                group_begin_=index_; group_end_=index_+1; group_loot_.reset();
                const auto& group=plan_.visits[index_].group;
                if (!group.empty()) while (group_end_<plan_.visits.size() && plan_.visits[group_end_].group==group) ++group_end_;
                for (auto i=group_begin_;i<group_end_;++i) evidence_[i]={};
                departed_=view.context.now;
                travelled_=true; phase_=Phase::arriving; stage(VisitStage::travelling);
                if (plan_.visits[index_].approach) return emit(RunProgram{plan_.visits[index_].approach});
                return emit(Navigate{plan_.visits[index_].destination});
            }
            ++index_;
        }
        if (travelled_ && plan_.finish_at) { phase_=Phase::returning; return emit(Navigate{*plan_.finish_at}); }
        phase_=Phase::done; return Done{"Selected visits finished"};
    }
public:
    VisitProgram(VisitPlan plan,std::shared_ptr<const game::RouteNetwork> network,
        std::shared_ptr<const AvailabilitySource> availability,std::shared_ptr<VisitResults> results)
        :plan_(std::move(plan)),availability_(std::move(availability)),results_(std::move(results)) {
        plan_.config.require_done=true;
        plan_.config.observe_interaction=std::any_of(plan_.visits.begin(),plan_.visits.end(),[](const auto& visit) { return std::holds_alternative<UseStation>(visit.action); });
        if (!network || !availability_ || plan_.visits.empty() || plan_.visits.size()>32 || !plan_.config.observe || plan_.config.backpack_limit
            || plan_.config.duration<Time(1000) || plan_.config.duration>std::chrono::hours(8)
            || !std::isfinite(plan_.config.speed) || plan_.config.speed<1 || plan_.config.speed>200
            || (plan_.finish_at && !network->contains(*plan_.finish_at))) throw std::invalid_argument("Invalid destination activity plan");
        evidence_.resize(plan_.visits.size());
        std::set<std::string> ids,groups;
        const Visit* previous=nullptr;
        const Visit* group_loot=nullptr;
        if (auto prepare=std::exchange(plan_.prepare,{})) prepare(plan_.visits);
        for (const auto& visit : plan_.visits) {
            if (visit.id.empty() || visit.id.size()>80 || !ids.insert(visit.id).second
                || std::any_of(visit.id.begin(),visit.id.end(),[](unsigned char c) { return c<32 || c==127; })
                || !network->contains(visit.destination) || visit.cooldown<Time::zero() || visit.cooldown>std::chrono::days(7)
                || visit.loot.size()>32 || visit.group.size()>80 || visit.settle<Time{} || visit.settle>Time(30000)
                || visit.loot_duration<Time{} || visit.loot_duration>Time(120000)
                || (!visit.group.empty() && visit.settle>Time{})
                || std::any_of(visit.group.begin(),visit.group.end(),[](unsigned char c) { return c<32 || c==127; }))
                throw std::invalid_argument("Invalid destination activity");
            if (!visit.group.empty()) {
                if (!std::holds_alternative<Pattern>(visit.action)) throw std::invalid_argument("Only observed patterns can share an encounter");
                if (previous && previous->group==visit.group) {
                    if (visit.destination!=previous->destination || bool(visit.departure)!=bool(previous->departure))
                        throw std::invalid_argument("Grouped activities need one destination, compatible departure and shared loot");
                } else {
                    if (!groups.insert(visit.group).second) throw std::invalid_argument("Activity groups must be consecutive");
                    group_loot=nullptr;
                }
                if (!visit.loot.empty()) {
                    if (group_loot && (visit.loot_duration!=group_loot->loot_duration || !std::equal(visit.loot.begin(),visit.loot.end(),group_loot->loot.begin(),group_loot->loot.end(),[](const Walk& a,const Walk& b) {
                        return a.tool==b.tool && a.leg.tiles==b.leg.tiles && a.leg.first==b.leg.first && a.leg.second==b.leg.second;
                    }))) throw std::invalid_argument("Grouped activities need shared loot movement");
                    group_loot=&visit;
                }
            }
            previous=&visit;
            if (const auto* station=std::get_if<UseStation>(&visit.action)) {
                if (visit.approach_scope || !visit.approach_location.empty()) throw std::invalid_argument("Station collection cannot be confirmed during approach");
                game::validate(game::TextTarget{station->name});
                if (station->timeout<Time(1000) || station->timeout>Time(60000)) throw std::invalid_argument("Invalid station timeout");
            } else {
                const auto& fight=std::get<Pattern>(visit.action);
                if (bool(fight.until)==bool(fight.outcome) || fight.backpack_limit || fight.duration<Time(1000) || fight.duration>std::chrono::hours(8)
                    || fight.legs.empty() || fight.legs.size()>128) throw std::invalid_argument("Combat requires a bounded fight and an observed result");
                if (fight.until) game::validate(*fight.until);
                for (const auto& leg : fight.legs) game::movement_slice(leg.leg,leg.leg.tiles,plan_.config.speed);
            }
            if (!visit.approach_location.empty() && visit.approach_location!=visit.destination)
                throw std::invalid_argument("Activity approach must describe its destination");
            double loot_duration=0;
            for (const auto& leg : visit.loot) {
                game::movement_slice(leg.leg,leg.leg.tiles,plan_.config.speed);
                loot_duration+=leg.leg.tiles*4000/plan_.config.speed;
            }
            if (loot_duration>30000) throw std::invalid_argument("Loot movement exceeds thirty seconds");
            results_->report_.visits.push_back({visit.id});
        }
    }
    RunConfig config() const override { return plan_.config; }
    bool request_yield() override { yield_=true; return true; }
    std::vector<Rect> text_regions() const override {
        if (index_>=plan_.visits.size() || (phase_!=Phase::arriving && phase_!=Phase::action && phase_!=Phase::settling)) return {};
        auto regions=arrived_?availability_->text_regions(plan_.visits[index_].id):std::vector<Rect>{};
        for (std::size_t i=group_begin_;i<group_end_;++i) if (evidence_[i].scoped) {
            if (const auto* pattern=std::get_if<Pattern>(&plan_.visits[i].action); pattern && pattern->until)
                regions.push_back(pattern->until->region);
        }
        return regions;
    }
    ActivityStatus activity_status() const override {
        if (index_>=plan_.visits.size()) return {};
        const auto& visit=plan_.visits[index_];
        if (phase_==Phase::arriving) return {"Travelling",visit.id};
        if (phase_==Phase::action) return {visit.working_status.empty()?(std::holds_alternative<UseStation>(visit.action)?"Collecting":"Fighting"):visit.working_status,visit.id};
        if (phase_==Phase::loot) return {"Collecting loot",visit.id};
        if (phase_==Phase::settling) return {"Waiting for rewards",visit.id};
        return {};
    }
    ProgramFactory recovery(Interruption reason,const View& view) override {
        if (!plan_.recovery || phase_==Phase::preparing || index_>=plan_.visits.size()
            || (reason!=Interruption::death && reason!=Interruption::lost_route && reason!=Interruption::lost_observations)) return {};
        {
            std::lock_guard lock(results_->mutex_);
            for (auto i=index_;i<std::max(index_+1,group_end_);++i)
                if (!results_->report_.visits[i].confirmed_at) results_->report_.visits[i].stage=VisitStage::interrupted;
        }
        index_=std::max(index_+1,group_end_); issued_=acknowledged_=view.completed_steps;
        arrived_.reset(); availability_since_.reset(); group_loot_.reset();
        prepared_=false; phase_=Phase::select;
        return plan_.recovery;
    }
private:
    void observe_member(const View& view,std::size_t index) {
        auto& evidence=evidence_[index];
        if (evidence.confirmed) return;
        const auto& visit=plan_.visits[index];
        if (visit.action_program) availability_->read(visit.id,view);
        if (!visit.approach_scope && visit.approach_location.empty() && (!arrived_ || visit.group.empty())) return;
        const auto& c=view.context;
        Time entered=arrived_.value_or(departed_);
        if (!visit.approach_location.empty()) {
            const auto& execution=view.route_execution;
            if (arrived_ && !execution) entered=evidence.scoped.value_or(*arrived_);
            else {
                if (!execution || !execution->usable(c) || execution->entered<departed_ || execution->to!=visit.destination) { evidence={}; return; }
                if (execution->observation_scope!=visit.approach_location) { evidence={}; return; }
                if (evidence.approach_segment && (evidence.approach_segment->route!=execution->route
                    || execution->checkpoint<evidence.approach_segment->checkpoint || execution->entered<evidence.approach_segment->entered
                    || (execution->checkpoint==evidence.approach_segment->checkpoint && execution->entered!=evidence.approach_segment->entered))) evidence={};
                evidence.approach_segment=ApproachSegment{execution->route,execution->checkpoint,execution->entered};
                entered=execution->entered;
            }
        }
        if (visit.approach_scope) {
            const auto scope=visit.approach_scope->read(c);
            if (!scope.has_value(c,Time(2000),departed_) || !scope.value) { evidence={}; return; }
            entered=std::max(entered,scope.captured);
        }
        if (!evidence.scoped) evidence.scoped=entered;
        if (visit.action_program) return;
        const auto present=availability_->read(visit.id,view);
        const auto& fight=std::get<Pattern>(visit.action);
        if (!fight.outcome && !view.observations) { return; }
        const auto result=fight.outcome?fight.outcome->read(c):game::read_text_target(c,*view.observations,*fight.until,*evidence.scoped);
        if (!result.usable(c,Time(2000),*evidence.scoped) || result.captured<evidence.last_outcome) { return; }
        if (result.captured==evidence.last_outcome) return;
        evidence.last_outcome=result.captured;
        if (!result.value) {
            evidence.clear_outcome=result.captured;
            if (present.has_value(c,Time(2000),*evidence.scoped) && present.value)
                evidence.seen_alive=std::max(evidence.seen_alive.value_or(present.captured),present.captured);
            return;
        }
        if (!evidence.clear_outcome || !evidence.seen_alive || result.captured<=*evidence.clear_outcome || result.captured<=*evidence.seen_alive
            || c.now-*evidence.seen_alive>Time(10000)) { return; }
        confirm(index,result.captured);
    }
public:
    void observe(const View& view) override {
        if (phase_!=Phase::arriving && phase_!=Phase::action) return;
        if (phase_==Phase::action && plan_.visits[index_].action_program
            && std::holds_alternative<UseStation>(plan_.visits[index_].action)) remember_timer(view);
        for (auto i=index_;i<group_end_;++i) {
            if (phase_==Phase::action && i==index_) continue;
            if (i!=index_ && plan_.visits[i].group.empty()) continue;
            observe_member(view,i);
        }
    }
    Step next(const View& view) override {
        if (view.completed_steps!=issued_ || acknowledged_!=issued_ || view.elapsed<last_
            || view.context.now<Time::zero() || view.context.now>Time::max()-std::chrono::days(7))
            throw std::logic_error("Activity progress is out of order");
        last_=view.elapsed;
        { std::lock_guard lock(results_->mutex_); results_->report_.state=State::running; results_->report_.generation=view.context.generation; results_->report_.window=view.context.window; }
        if (phase_==Phase::preparing) { phase_=Phase::select; availability_since_.reset(); }
        if (phase_==Phase::departing) advance();
        if (phase_==Phase::arriving) {
            if (evidence_[index_].confirmed) return finish_member(view);
            else {
                const auto readiness=available(view);
                if (readiness==Availability::waiting && !plan_.visits[index_].action_program) return emit(Wait{Time(100),"Checking activity availability after travel"});
                if (readiness==Availability::skip) return finish_member(view);
                phase_=Phase::action; stage(VisitStage::working);
                if (const auto create=plan_.visits[index_].action_program)
                    return emit(RunProgram{[create,started=evidence_[index_].scoped.value_or(arrived_.value_or(departed_))] { return create(started); }});
                auto action=std::visit([](const auto& action)->Step { return action; },plan_.visits[index_].action);
                if (auto* pattern=std::get_if<Pattern>(&action); pattern && evidence_[index_].clear_outcome) {
                    const auto& c=view.context;
                    pattern->outcome_baseline=Observation<bool>{Knowledge::absent,false,*evidence_[index_].clear_outcome,c.window,c.generation};
                }
                return emit(std::move(action));
            }
        }
        if (phase_==Phase::action || phase_==Phase::settling) return finish_member(view);
        if (phase_==Phase::loot) {
            const auto& sequence=plan_.visits[*group_loot_].loot;
            if (loot_==sequence.size() && view.context.now-loot_started_<plan_.visits[*group_loot_].loot_duration) loot_=0;
            if (loot_<sequence.size()) return emit(sequence[loot_++]);
            return leave(view);
        }
        if (phase_==Phase::select) return select(view);
        if (phase_==Phase::returning) { phase_=Phase::done; return Done{"Selected visits finished"}; }
        throw std::logic_error("Activity has already finished");
    }
    void step_completed(const View& view) override {
        if (view.completed_steps!=issued_ || acknowledged_+1!=issued_) throw std::logic_error("Unexpected activity completion");
        acknowledged_=issued_;
        if (phase_==Phase::arriving && !arrived_) arrived_=view.context.now+Time(1);
        if (phase_==Phase::action) {
            const auto retry=plan_.visits[index_].retry_after_completion?plan_.visits[index_].retry_after_completion():std::optional<Time>{};
            if (retry) {
                if (*retry<=Time{} || *retry>std::chrono::days(7)) throw std::logic_error("Invalid activity retry interval");
                std::lock_guard lock(results_->mutex_);
                auto& result=results_->report_.visits[index_];
                result.stage=VisitStage::deferred;
                result.observed_ready_at=view.context.now+*retry;
            } else {
                auto& visit=plan_.visits[index_];
                if (visit.cooldown_after_completion) {
                    const auto cooldown=visit.cooldown_after_completion();
                    if (cooldown<=Time{} || cooldown>std::chrono::days(7)) throw std::logic_error("Invalid activity cooldown");
                    visit.cooldown=cooldown;
                }
                confirm(index_,view.context.now);
                if (visit.cooldown_after_completion) {
                    std::lock_guard lock(results_->mutex_);
                    auto& result=results_->report_.visits[index_];
                    result.observed_ready_at=result.estimated_ready_at;
                }
            }
        } else if (phase_==Phase::loot && loot_==plan_.visits[*group_loot_].loot.size()
            && view.context.now-loot_started_>=plan_.visits[*group_loot_].loot_duration) {
            std::lock_guard lock(results_->mutex_);
            for (auto i=group_begin_;i<group_end_;++i)
                if (evidence_[i].confirmed) results_->report_.visits[i].stage=VisitStage::completed;
        }
    }
    void finished(State state) override {
        std::lock_guard lock(results_->mutex_); results_->report_.state=state;
        for (auto& result : results_->report_.visits) if (result.stage==VisitStage::preparing || result.stage==VisitStage::travelling || result.stage==VisitStage::working || result.stage==VisitStage::awaiting_loot || result.stage==VisitStage::looting || result.stage==VisitStage::settling)
            result.stage=VisitStage::interrupted;
    }
};
VisitRun visit_program(VisitPlan plan,std::shared_ptr<const game::RouteNetwork> network,std::shared_ptr<const AvailabilitySource> availability) {
    auto results=std::make_shared<VisitResults>();
    auto program=std::make_unique<VisitProgram>(std::move(plan),std::move(network),std::move(availability),results);
    return {std::move(program),std::move(results)};
}
}
