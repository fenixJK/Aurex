#include "execution.hpp"
#include "modules/shared/movement.hpp"
#include "observations.hpp"
#include <algorithm>
#include <thread>

namespace hive::features::planters {
using namespace automation;
ExecutionReport ExecutionResults::read() const { std::lock_guard lock(mutex_); return report_; }
class Execution final : public automation::Program {
    ExecutionPlan plan_;
    std::shared_ptr<Account> account_;
    std::shared_ptr<const Source> source_;
    std::shared_ptr<const Actions> actions_;
    WallClock clock_;
    std::shared_ptr<ExecutionResults> results_;
    std::unique_ptr<Controller> controller_;
    TravelTimes travel_;
    std::thread worker_;
    std::atomic_bool planned_{},cancelled_{};
    PlanDecision decision_;
    std::exception_ptr error_;
    std::optional<Assignment> assignment_;
    std::optional<std::uint64_t> harvest_;
    std::set<std::uint64_t> deferred_;
    std::set<std::string> failed_fields_;
    std::set<std::string> unavailable_planters_;
    std::vector<Step> steps_;
    std::size_t action_step_{},loot_step_{},issued_{},acknowledged_{},confirmed_{},placements_{},harvests_{};
    unsigned recoveries_{};
    enum class Phase { select, initializing, planning, preparing, arriving, locating, approaching, baseline, acting, placed, confirming, looting, departing, returning, checking_backpack, converting, done } phase_{Phase::select};
    std::optional<ActivityStatus> pending_report_;
    Time started_{},deadline_{},planned_at_{},minimum_{},travel_started_{};
    bool leased_{},harvest_confirmed_{};
    std::optional<WallTime> last_wall_;
    Stats selected_;
    std::string finish_reason_;
    bool deferred_result_{};
    std::optional<bool> learn_travel_;
    bool had_growth_bar_{},harvest_action_{},initialized_{},ready_for_travel_{};
    bool capacity_armed_{},capacity_rejected_{};
    Time placement_started_{};
    std::shared_ptr<Observation<workflows::InventoryMatch>> placement_inventory_;
    bool harvest_action() const {
        if (steps_.empty() || steps_.size()>2) return false;
        const auto* press=std::get_if<Hold>(&steps_.front());
        if (!press || press->keys!=std::vector{Key::interact}) return false;
        if (steps_.size()==1) return true;
        const auto* answer=std::get_if<AnswerDialog>(&steps_.back());
        return answer && answer->plan.choice==workflows::DialogChoice::yes;
    }
    Step emit(Step step) { ++issued_; return step; }
    Step wait(const std::string& reason) { return emit(Wait{Time(50),reason}); }
    void publish(ExecutionStage stage) {
        std::lock_guard lock(results_->mutex_);
        auto& report=results_->report_; report.state=State::running; report.stage=stage; report.confirmed=confirmed_; report.placements=placements_; report.harvests=harvests_;
        report.field=selected_.field; report.planter=selected_.planter;
        if (controller_) {
            report.growing=std::count_if(controller_->ledger().plants.begin(),controller_->ledger().plants.end(),[](const Plant& plant) { return !plant.missing; }); report.pending=controller_->ledger().pending;
            account_->publish(controller_->ledger());
            report.records.clear();
            for (const auto& plant : controller_->ledger().plants) report.records.emplace_back(plant.id,identity(plant.stats));
        }
    }
    std::string planter_target() const {
        std::string name=selected_.planter;
        for (const auto& item:equipment()) if (item.id==name) { name=item.label; break; }
        if (name.find("Planter")==std::string::npos) name+=" Planter";
        return name+" · "+selected_.field;
    }
    void report_outcome() {
        pending_report_=ActivityStatus{harvest_?"Planter harvested":"Planter placed",planter_target()};
    }
    void complete_action() {
        recoveries_=0;
        ++confirmed_; if (harvest_) ++harvests_; else ++placements_; loot_step_=0; report_outcome();
        if (harvest_) { harvest_confirmed_=true; phase_=Phase::looting; publish(ExecutionStage::looting); }
        else { phase_=Phase::placed; publish(ExecutionStage::confirming); }
    }
    void settle_action() {
        if (!controller_ || !controller_->ledger().pending) return;
        // Dragging only opens the purchase dialog. Before its answer is issued,
        // an inventory failure or cancellation cannot have placed a planter.
        if (capacity_rejected_ && controller_->ledger().pending->operation==Operation::place)
            controller_->reject_capacity(controller_->ledger().pending->ticket);
        else if (controller_->ledger().pending->operation==Operation::place && phase_==Phase::acting
            && action_step_==1 && std::holds_alternative<DragInventoryItem>(steps_.front()))
            controller_->abandon_unstarted_placement(controller_->ledger().pending->ticket,wall());
        else controller_->reserve_unconfirmed();
        publish(ExecutionStage::checking);
    }
    Step defer_result(const View& view,std::string reason="Planter outcome is unclear; check Timers before retrying") {
        settle_action();
        controller_->interrupt();
        deferred_result_=true;
        finish_reason_=std::move(reason);
        pending_report_=ActivityStatus{"Planter not confirmed",finish_reason_,ActivityKind::failure};
        phase_=Phase::placed; publish(ExecutionStage::returning);
        return next(view);
    }
    Step failed_placement(const View& view,const std::string& reason) {
        account_->defer_placements(wall());
        return defer_result(view,reason+"; placements deferred for five minutes. Capacity unchanged.");
    }
    void observe_capacity(const View& view) {
        if (harvest_ || (phase_!=Phase::baseline && phase_!=Phase::acting && phase_!=Phase::confirming)) return;
        const auto warning=placement_limit(view);
        const auto since=phase_==Phase::baseline?minimum_:placement_started_;
        if (!warning.usable(view.context,Time(2000),since+Time(1))) return;
        if (!warning.value) capacity_armed_=true;
        else if (capacity_armed_ && controller_ && controller_->ledger().pending && warning.captured>placement_started_)
            capacity_rejected_=true;
    }
    void mark_missing() {
        if (!harvest_ || !controller_->mark_missing(*harvest_)) return;
        pending_report_=ActivityStatus{"Planter not found",selected_.field+" · slot reserved; correct or remove its timer",ActivityKind::failure};
        publish(ExecutionStage::checking);
    }
    const Plant* due_plant(WallTime now) const {
        const Plant* due=nullptr;
        WallTime earliest;
        for (const auto& plant : controller_->ledger().plants) {
            if (plant.missing || deferred_.contains(plant.id) || failed_fields_.contains(plant.stats.field) || !plan_.destinations.contains(plant.stats.field)) continue;
            const auto ready=ready_at(plant);
            const auto travel=travel_.estimate(plant.stats.field);
            const auto departure=ready-Time(static_cast<Time::rep>(travel.learned?travel.seconds*1000:0));
            if (departure<=now && (!due || ready<earliest)) { due=&plant; earliest=ready; }
        }
        return due;
    }
    WallTime wall() {
        const auto value=clock_();
        if (value.time_since_epoch()<Time::zero() || value.time_since_epoch()>Time::max()-std::chrono::hours(24)
            || (last_wall_ && value<*last_wall_)) throw std::runtime_error("Planter account clock changed; restart after checking the clock");
        last_wall_=value; return value;
    }
    bool scan(const View& view,const Snapshot& snapshot) const {
        if (phase_==Phase::baseline && !harvest_ && plan_.attempt_unscanned_placement
            && !snapshot.placements.has_value(view.context,Time(2000),minimum_)) return true;
        const bool presence_allowed=(phase_==Phase::baseline && harvest_) || (phase_==Phase::confirming && !harvest_);
        return snapshot.placements.has_value(view.context,Time(2000),minimum_)
            && (snapshot.placements.value.complete || (presence_allowed && snapshot.placements.value.identities.contains(identity(selected_)))
                || (phase_==Phase::baseline && !harvest_ && plan_.attempt_unscanned_placement && snapshot.placements.value.field==selected_.field))
            && (snapshot.placements.value.field.empty() || snapshot.placements.value.field==selected_.field);
    }
    std::vector<Assignment> refresh(const View& view,std::vector<Group> groups,WallTime now,bool selected_only) const {
        for (auto& group : groups) {
            std::erase_if(group.candidates,[&](const auto& candidate) {
                const auto& s=candidate.stats;
                if (unavailable_planters_.contains(s.planter)) return true;
                const auto matches=[&](const Stats& other) { return s.planter==other.planter && s.field==other.field && s.nectar==other.nectar; };
                return selected_only?!matches(selected_):std::none_of(decision_.assignments.begin(),decision_.assignments.end(),[&](const auto& a) { return matches(a.stats); });
            });
            for (auto& candidate : group.candidates) {
                candidate.travel=travel_.estimate(candidate.stats.field);
            }
        }
        auto current=planning_snapshot(controller_->ledger(),std::move(groups),travel_,now,view.context.now.count()/1000.0,plan_.capacity);
        std::vector<Active> active;
        for (const auto& plant : current.growing) active.push_back({plant.stats.planter,plant.stats.field,plant.material});
        if (plan_.preferences) apply_preferences(current.materials,*plan_.preferences);
        current.materials.goals.clear();
        for (const auto& choice:decision_.assignments) if (!choice.material.empty() && (!selected_only || identity(choice.stats)==identity(selected_)))
            current.materials.goals.push_back(material_goal(choice.material));
        const auto updated=planning_groups(current);
        return placement_choices(updated,active,current.capacity,decision_.selection,current.materials,selected_only);
    }
    void validate_actions(const std::vector<Step>& steps) const {
        if (steps.empty() || steps.size()>32) throw std::invalid_argument("Planter actions need a bounded sequence");
        for (const auto& step : steps) {
            if (const auto* item=std::get_if<DragInventoryItem>(&step)) {
                workflows::validate(item->plan);
                if (!item->source) throw std::invalid_argument("Planter inventory observations are missing");
            } else if (const auto* answer=std::get_if<AnswerDialog>(&step)) {
                workflows::validate(answer->plan);
            } else if (const auto* prompt=std::get_if<UsePrompt>(&step)) {
                game::validate(prompt->prompt); game::validate(prompt->result);
                if (prompt->timeout<Time(1000) || prompt->timeout>Time(10000)) throw std::invalid_argument("Invalid planter prompt timeout");
            } else if (const auto* approach=std::get_if<ApproachLandmark>(&step)) {
                workflows::validate(approach->movement);
                if (!approach->source || approach->movement.completion!=workflows::CheckpointCompletion::observation)
                    throw std::invalid_argument("Planter approach needs observed proximity");
            } else if (const auto* action=std::get_if<UseStation>(&step)) {
                game::validate(game::TextTarget{action->name});
                if (action->timeout<Time(1000) || action->timeout>Time(10000)) throw std::invalid_argument("Invalid planter interaction timeout");
            } else if (const auto* walk=std::get_if<Walk>(&step)) {
                if (walk->tool) throw std::invalid_argument("Planter positioning cannot use the gather tool");
                game::movement_slice(walk->leg,walk->leg.tiles,plan_.config.speed);
            } else if (const auto* press=std::get_if<Hold>(&step)) {
                if (!harvest_ || press->keys!=std::vector{Key::interact} || press->pointer
                    || press->duration<Time(1) || press->duration>Time(1000)) throw std::invalid_argument("Invalid planter harvest input");
            } else if (const auto* wait=std::get_if<AwaitText>(&step)) {
                game::validate(wait->target);
                if (wait->timeout<Time(100) || wait->timeout>Time(10000)) throw std::invalid_argument("Invalid planter observation timeout");
            } else if (const auto* wait=std::get_if<Wait>(&step)) {
                if (wait->duration<Time(1) || wait->duration>Time(1000) || wait->reason.size()>160) throw std::invalid_argument("Invalid planter wait");
            } else throw std::invalid_argument("Unsupported planter action; use observed game operations");
        }
    }
    Step navigate(const View& view) {
        learn_travel_.reset();
        phase_=Phase::arriving; minimum_=travel_started_=view.context.now; publish(ExecutionStage::travelling);
        Navigate destination{plan_.destinations.at(selected_.field)};
        if (!harvest_) destination.inventory_search=actions_->inspect(selected_);
        return emit(std::move(destination));
    }
    Step travel(const View& view) {
        if (!plan_.destinations.contains(selected_.field)) throw std::runtime_error("No connected destination for planter field");
        if (ready_for_travel_) { ready_for_travel_=false; return navigate(view); }
        if (plan_.setup) {
            phase_=Phase::preparing; publish(ExecutionStage::preparing);
            return emit(RunProgram{plan_.setup});
        }
        return navigate(view);
    }
    Step complete_visit(const View& view) {
        if (!finish_reason_.empty()) {
            phase_=Phase::done; publish(ExecutionStage::finished);
            if (deferred_result_) return Defer{finish_reason_};
            return Done{finish_reason_};
        }
        phase_=Phase::select; placement_inventory_.reset(); selected_={}; assignment_.reset(); harvest_.reset(); harvest_confirmed_=false;
        minimum_=view.context.now; deadline_=view.context.now+Time(10000); publish(ExecutionStage::checking);
        return wait("Checking the next planter action");
    }
    void begin_baseline(const View& view) {
        phase_=Phase::baseline;
        capacity_armed_=capacity_rejected_=false;
        minimum_=view.context.now;
        deadline_=view.context.now+Time(10000);
        publish(ExecutionStage::checking);
    }
    Step return_home(const View& view) {
        if (plan_.return_to) {
            phase_=Phase::returning; publish(ExecutionStage::returning);
            return emit(Navigate{*plan_.return_to});
        }
        return complete_visit(view);
    }
    Step leave(const View& view) {
        const auto visit=plan_.visits.find(selected_.field);
        if (visit!=plan_.visits.end()) {
            const auto& departure=plan_.walk_return?visit->second.walking_departure:visit->second.departure;
            if (!departure) return return_home(view);
            phase_=Phase::departing; publish(ExecutionStage::returning);
            return emit(RunProgram{departure});
        }
        return return_home(view);
    }
    void release() noexcept {
        if (worker_.joinable()) { cancelled_=true; worker_.join(); }
        if (controller_) controller_->interrupt();
        if (leased_) { account_->busy_=false; leased_=false; }
    }
public:
    Execution(ExecutionPlan plan,std::shared_ptr<const game::RouteNetwork> network,std::shared_ptr<Account> account,
        std::shared_ptr<const Source> source,std::shared_ptr<const Actions> actions,WallClock clock,std::shared_ptr<ExecutionResults> results)
        :plan_(std::move(plan)),account_(std::move(account)),source_(std::move(source)),actions_(std::move(actions)),clock_(std::move(clock)),results_(std::move(results)) {
        if (!network || !account_ || !source_ || !actions_ || !clock_ || plan_.destinations.empty() || plan_.destinations.size()>32
            || plan_.capacity<1 || plan_.capacity>3 || plan_.max_actions<1 || plan_.max_actions>32 || !plan_.preview_steps || plan_.preview_steps>100000
            || !plan_.config.observe || plan_.config.backpack_limit || plan_.config.duration<Time(1000) || plan_.config.duration>std::chrono::hours(8)
            || !std::isfinite(plan_.config.speed) || plan_.config.speed<1 || plan_.config.speed>200) throw std::invalid_argument("Invalid planter execution services or settings");
        for (const auto& [field,destination] : plan_.destinations) if (field.empty() || field.size()>80 || !network->contains(destination))
            throw std::invalid_argument("Invalid planter field destination");
        if (plan_.walk_return) for (const auto& [field,destination] : plan_.destinations) {
            const auto visit=plan_.visits.find(field);
            if (visit==plan_.visits.end() || !visit->second.walking_departure)
                throw std::invalid_argument("Missing walking planter return");
        }
        if (plan_.return_to && !network->contains(*plan_.return_to)) throw std::invalid_argument("Invalid planter return destination");
        if (plan_.conversion && (!plan_.return_to || !std::isfinite(plan_.conversion->threshold)
            || plan_.conversion->threshold<1 || plan_.conversion->threshold>100)) throw std::invalid_argument("Planter conversion needs a return destination and valid backpack threshold");
        if (plan_.movement) {
            const auto recipes=movement::snapshot(plan_.movement);
            for (auto& [field,visit]:plan_.visits) visit.loot=movement_walks(*recipes,"planter-loot",{{"field",field}});
        }
        for (const auto& [field,visit] : plan_.visits) {
            if (!plan_.destinations.contains(field) || visit.loot.size()>128) throw std::invalid_argument("Invalid planter field visit");
            double duration=0;
            for (const auto& walk : visit.loot) {
                if (walk.tool) throw std::invalid_argument("Planter loot cannot use the gather tool");
                game::movement_slice(walk.leg,walk.leg.tiles,plan_.config.speed);
                duration+=walk.leg.tiles*4000/plan_.config.speed;
            }
            if (duration>60000) throw std::invalid_argument("Planter loot movement exceeds one minute");
        }
        plan_.config.require_done=true;
        if (plan_.preferences) validate(*plan_.preferences);
    }
    ~Execution() override { release(); }
    RunConfig config() const override { return plan_.config; }
    std::vector<game::FixedLabelQuery> fixed_labels() const override {
        if (!harvest_ && (phase_==Phase::baseline || phase_==Phase::acting || phase_==Phase::confirming))
            return {placement_limit_query()};
        return {};
    }
    ActivityStatus activity_status() const override {
        if (selected_.field.empty()) return {};
        const auto target=planter_target();
        switch (phase_) {
        case Phase::preparing: return {};
        case Phase::arriving: return {"Travelling",target};
        case Phase::departing: case Phase::returning: case Phase::placed: case Phase::checking_backpack: return {"Returning to hive",{}};
        case Phase::converting: return {"Converting","Backpack"};
        case Phase::looting: return {"Collecting planter loot",selected_.field};
        case Phase::select: case Phase::initializing: case Phase::planning: case Phase::done: return {};
        default: return {harvest_?"Harvesting planter":"Placing planter",target};
        }
    }
    ProgramFactory recovery(Interruption reason,const View& view) override {
        if ((reason==Interruption::lost_observations || reason==Interruption::lost_route)
            && (phase_==Phase::approaching || phase_==Phase::baseline || phase_==Phase::locating)) mark_missing();
        // A missing placement result cannot be fixed by resetting and repeating the placement.
        if (reason==Interruption::lost_observations && !harvest_
            && (phase_==Phase::acting || phase_==Phase::confirming)) return {};
        const auto recover=reason==Interruption::death?plan_.death_recovery:plan_.route_recovery;
        if ((reason!=Interruption::death && reason!=Interruption::lost_route && reason!=Interruption::lost_observations)
            || !recover || recoveries_>=3) return {};
        if (reason==Interruption::death && !selected_.field.empty()) failed_fields_.insert(selected_.field);
        ++recoveries_; resume_after_reconnect(view); return recover;
    }
    bool resume_after_reconnect(const View& view) override {
        if (worker_.joinable()) { cancelled_=true; worker_.join(); }
        settle_action();
        if (controller_) controller_->interrupt();
        planned_=false; cancelled_=false; error_={}; decision_={};
        assignment_.reset(); harvest_.reset(); steps_.clear(); placement_inventory_.reset();
        action_step_=loot_step_=0; issued_=acknowledged_=view.completed_steps;
        phase_=Phase::select; selected_={}; finish_reason_.clear(); deferred_result_=false; learn_travel_.reset();
        harvest_confirmed_=harvest_action_=had_growth_bar_=initialized_=ready_for_travel_=false;
        capacity_armed_=capacity_rejected_=false;
        minimum_=view.context.now; deadline_=minimum_+Time(10000);
        publish(ExecutionStage::preparing);
        return true;
    }
    void observe(const View& view) override {
        observe_capacity(view);
        if (phase_!=Phase::arriving || learn_travel_ || !view.route_execution || !view.route_execution->usable(view.context)
            || view.route_execution->entered<travel_started_) return;
        learn_travel_=plan_.return_to && view.route_execution->from==*plan_.return_to;
    }
    Step next(const View& view) override {
        if (view.completed_steps!=issued_ || acknowledged_!=issued_ || view.context.now<Time::zero() || view.context.now>Time::max()-Time(30000))
            throw std::logic_error("Invalid planter execution progress");
        if (!leased_) {
            if (account_->busy_.exchange(true)) throw std::runtime_error("Another run owns this planter account");
            leased_=true; controller_=std::make_unique<Controller>(account_->store_,plan_.capacity);
            travel_={};
            for (const auto& [field,samples]:controller_->ledger().travel) for (double seconds:samples) travel_.record(field,seconds);
            started_=minimum_=view.context.now; deadline_=view.context.now+Time(10000);
        }
        account_->apply_offline(*controller_);
        observe_capacity(view);
        if (capacity_rejected_ && controller_->ledger().pending)
            return defer_result(view,"Three-planter limit confirmed; keeping only the managed slots. Reconcile missing planters in Timers.");
        if (phase_==Phase::preparing) {
            initialized_=true;
            return navigate(view);
        }
        if (pending_report_) {
            auto status=std::move(*pending_report_); pending_report_.reset();
            return emit(ReportActivity{std::move(status)});
        }
        if (phase_==Phase::placed) return leave(view);
        if (phase_==Phase::locating && !harvest_) {
            begin_baseline(view);
            return wait("Preparing planter placement");
        }
        if (phase_==Phase::locating) {
            const auto snapshot=source_->read_at(view,selected_);
            const auto& local=snapshot.placements;
            if (!local.has_value(view.context,Time(2000),minimum_)) {
                if (view.context.now<deadline_) return wait("Reading planter harvest prompt");
                mark_missing(); phase_=Phase::placed; return next(view);
            }
            const bool locate=!local.value.identities.contains(identity(selected_));
            if (locate) if (auto approach=actions_->locate(selected_)) {
                workflows::validate(approach->movement);
                if (!approach->source || approach->movement.completion!=workflows::CheckpointCompletion::observation)
                    throw std::invalid_argument("Locating a planter needs observed proximity");
                phase_=Phase::approaching; publish(ExecutionStage::locating);
                return emit(std::move(*approach));
            }
            begin_baseline(view);
            return wait("Reading planters in the field");
        }
        if (phase_==Phase::departing) return return_home(view);
        if (phase_==Phase::returning || phase_==Phase::converting) return complete_visit(view);
        if (phase_==Phase::checking_backpack) {
            if (!plan_.conversion->every_return) {
                if (view.context.now>=deadline_) throw std::runtime_error("Fresh backpack reading unavailable after planter harvest");
                if (!view.observations || !view.observations->backpack_percent.has_value(view.context,Time(2000),minimum_))
                    return wait("Reading backpack after harvest");
                const auto pack=view.observations->backpack_percent.value;
                if (!std::isfinite(pack) || pack<0 || pack>100) throw std::runtime_error("Invalid backpack reading after planter harvest");
                if (pack<plan_.conversion->threshold) return complete_visit(view);
            }
            phase_=Phase::converting; publish(ExecutionStage::converting); return emit(Convert{});
        }
        if (phase_==Phase::looting) {
            const auto visit=plan_.visits.find(selected_.field);
            if (plan_.collect_loot && visit!=plan_.visits.end() && loot_step_<visit->second.loot.size()) {
                publish(ExecutionStage::looting); return emit(visit->second.loot[loot_step_++]);
            }
            return leave(view);
        }
        if (phase_==Phase::select && !controller_->ledger().pending && confirmed_>=plan_.max_actions) {
            phase_=Phase::done; publish(ExecutionStage::finished); return Done{"Planter action limit reached"};
        }
        if (phase_==Phase::acting && placement_inventory_ && placement_inventory_->knowledge==Knowledge::absent) {
            controller_->abandon_unstarted_placement(controller_->ledger().pending->ticket,wall());
            unavailable_planters_.insert(selected_.planter);
            return leave(view);
        }
        if (phase_==Phase::acting) {
            if (!harvest_ && action_step_==1 && std::holds_alternative<DragInventoryItem>(steps_.front())) {
                // A rejected drag may never open Yes/No. Check the shared dialog
                // observation and capacity warning before handing over to it.
                if (!view.observations || !view.observations->confirmation.has_value(view.context,Time(2000),minimum_)) {
                    if (view.context.now>=deadline_) return failed_placement(view,"Placement confirmation did not appear");
                    return wait("Waiting for planter confirmation or rejection");
                }
            }
            return emit(steps_.at(action_step_++));
        }
        if (phase_==Phase::planning && !planned_.load(std::memory_order_acquire)) return wait("Planning planter assignments");
        const auto now=wall();
        if (phase_==Phase::select) {
            if (controller_->ledger().pending) {
                selected_=controller_->ledger().pending->plant.stats;
                settle_action();
                return emit(ReportActivity{{"Saved planter needs review",planter_target()+" · previous action unconfirmed; slot reserved. Correct or remove its timer."}});
            }
            if (const auto* due=due_plant(now)) {
                selected_=due->stats; harvest_=due->id; assignment_.reset(); return travel(view);
            }
            if (controller_->ledger().plants.size()>=placement_capacity(controller_->ledger().placement_limits,plan_.capacity)) {
                phase_=Phase::done; publish(ExecutionStage::finished); return Done{"No planter visit is due"};
            }
            if (const auto retry=account_->placement_retry();retry && now<*retry) {
                phase_=Phase::done; publish(ExecutionStage::finished); return Done{"Placement retry is deferred; known harvests remain available"};
            }
            account_->resume_placements(now);
        }
        if (phase_==Phase::planning) {
            if (worker_.joinable()) { worker_.join(); if (error_) std::rethrow_exception(error_); deadline_=view.context.now+Time(10000); }
            if (const auto* due=due_plant(now)) {
                selected_=due->stats; harvest_=due->id; assignment_.reset(); return travel(view);
            }
        }
        const bool at_field=(phase_==Phase::baseline && (harvest_ || !plan_.attempt_unscanned_placement))
            || (phase_==Phase::confirming && harvest_);
        auto snapshot=at_field?source_->read_at(view,selected_):
            phase_==Phase::confirming?Snapshot{}:source_->read(view);
        const bool planning=phase_==Phase::select || phase_==Phase::planning || (phase_==Phase::baseline && !harvest_);
        const bool requires_nectar=!plan_.preferences || plan_.preferences->mode==Mode::nectar;
        const bool fresh=snapshot.nectar.has_value(view.context,Time(2000));
        auto groups=fresh?snapshot.nectar.value:std::vector<Group>{};
        if (planning && !requires_nectar && groups.empty()) groups=candidate_groups();
        if (!groups.empty()) {
            if (!planning) for (auto& group : groups) group.candidates.clear();
            validate_configuration(groups,{},plan_.capacity);
            account_->prepare_groups(groups,plan_.preferences);
            for (auto& group:groups) std::erase_if(group.candidates,[&](const auto& candidate) {
                return failed_fields_.contains(candidate.stats.field) || !plan_.destinations.contains(candidate.stats.field);
            });
        }
        if (phase_==Phase::select) {
            if (requires_nectar && !snapshot.nectar.has_value(view.context,Time(2000),started_)) {
                if (!initialized_ && plan_.setup) {
                    initialized_=true; phase_=Phase::initializing; publish(ExecutionStage::preparing);
                    return emit(RunProgram{plan_.setup});
                }
                if (view.context.now>=deadline_) return Defer{"Planter check deferred: fresh nectar readings are unavailable"};
                publish(ExecutionStage::planning); return wait("Waiting for nectar readings");
            }
            for (auto& group : groups) {
                std::erase_if(group.candidates,[&](const auto& c) {
                    return !plan_.destinations.contains(c.stats.field) || unavailable_planters_.contains(c.stats.planter);
                });
                for (auto& candidate : group.candidates) {
                    candidate.travel=travel_.estimate(candidate.stats.field);
                }
            }
            auto planning=planning_snapshot(controller_->ledger(),std::move(groups),travel_,now,view.context.now.count()/1000.0,plan_.capacity);
            if (plan_.preferences) apply_preferences(planning.materials,*plan_.preferences);
            planned_=false; cancelled_=false; error_={}; planned_at_=snapshot.nectar.captured;
            worker_=std::thread([this,planning=std::move(planning)] {
                try { decision_=account_->planner_.select(planning,{plan_.preview_steps,[this] { return cancelled_.load(); }}); }
                catch (...) { error_=std::current_exception(); }
                planned_.store(true,std::memory_order_release);
            });
            phase_=Phase::planning; publish(ExecutionStage::planning); return wait("Planning planter assignments");
        }
        if (phase_==Phase::planning) {
            if (requires_nectar && !snapshot.nectar.has_value(view.context,Time(2000),planned_at_)) {
                if (view.context.now>=deadline_) throw std::runtime_error("Nectar could not be refreshed after planning");
                return wait("Refreshing nectar after planning");
            }
            if (decision_.assignments.empty()) { phase_=Phase::done; publish(ExecutionStage::finished); return Done{"No planter action is due"}; }
            const auto assignments=refresh(view,std::move(groups),now,false);
            if (assignments.empty()) { phase_=Phase::done; publish(ExecutionStage::finished); return Done{"Planned assignments are no longer needed or available"}; }
            assignment_=assignments.front(); selected_=assignment_->stats; harvest_.reset(); return travel(view);
        }
        if (phase_==Phase::baseline) {
            if (!harvest_ && view.context.now>=deadline_) return failed_placement(view,"Placement location could not be read");
            if (!harvest_) {
                const auto prompt=source_->placement_visible(view);
                if (!prompt.usable(view.context,Time(2000),minimum_)) return wait("Reading placement location");
                // A planter already visible before the drag cannot confirm a new placement.
                if (prompt.value) {
                    controller_->record_occupied(selected_.field);
                    return defer_result(view,"A planter is already here; field and slot reserved. Other planters can continue; reconcile it in Timers.");
                }
            }
            if (!scan(view,snapshot)) {
                if (view.context.now>=deadline_) {
                    if (harvest_) { mark_missing(); phase_=Phase::placed; return next(view); }
                    throw ObservationUnavailable("Planter field state could not be confirmed");
                }
                return wait("Reading planters in the field");
            }
            if (view.context.now>=deadline_) throw ObservationUnavailable("Planter reading could not be completed");
            if (harvest_) {
                if (!snapshot.placements.value.identities.contains(identity(selected_))) {
                    mark_missing(); phase_=Phase::placed; return next(view);
                }
                const auto& plants=controller_->ledger().plants;
                const auto found=std::find_if(plants.begin(),plants.end(),[&](const Plant& p) { return p.id==*harvest_; });
                if (now<ready_at(*found)) {
                    deferred_.insert(*harvest_); return leave(view);
                }
            }
            if (!harvest_) {
                if (requires_nectar && !snapshot.nectar.has_value(view.context,Time(2000),minimum_)) return wait("Refreshing nectar before placement");
                const auto assignments=refresh(view,std::move(groups),now,true);
                if (assignments.empty()) {
                    finish_reason_="Selected planter is no longer needed or available"; return leave(view);
                }
                assignment_=assignments.front(); selected_=assignment_->stats;
            }
            steps_=actions_->prepare(harvest_?Operation::harvest:Operation::place,selected_); validate_actions(steps_);
            placement_inventory_.reset();
            if (!harvest_ && !steps_.empty())
                if (const auto* drag=std::get_if<DragInventoryItem>(&steps_.front())) placement_inventory_=drag->result;
            harvest_action_=harvest_ && harvest_action();
            had_growth_bar_=snapshot.growth_bar.has_value(view.context,Time(2000),minimum_) && snapshot.growth_bar.value;
            if (harvest_) controller_->harvest(*harvest_,view.context,now,snapshot.placements,{});
            else {
                controller_->place(*assignment_,view.context,now,snapshot.placements,plan_.attempt_unscanned_placement);
                placement_started_=view.context.now;
            }
            action_step_=0; phase_=Phase::acting; publish(ExecutionStage::acting);
        }
        if (phase_==Phase::acting) return emit(steps_.at(action_step_++));
        if (phase_==Phase::confirming) {
            auto evidence=harvest_ && scan(view,snapshot)?snapshot.placements:Observation<Placements>{};
            if (!harvest_) {
                const auto prompt=source_->placement_visible(view);
                if (prompt.usable(view.context,Time(2000),minimum_)) {
                    Placements local{{},true,selected_.field};
                    if (prompt.value) local.identities.insert(identity(selected_));
                    evidence={Knowledge::present,std::move(local),prompt.captured,prompt.window,prompt.generation};
                }
            }
            const auto receipt=harvest_action_?harvest_receipt(selected_,had_growth_bar_,snapshot,view,minimum_):Observation<HarvestEvidence>{};
            const auto result=controller_->observe(controller_->ledger().pending->ticket,view.context,now,evidence,receipt);
            if (result==Outcome::failed) return failed_placement(view,"No planter appeared after placement");
            if (result==Outcome::uncertain) return defer_result(view);
            if (result==Outcome::confirmed) {
                complete_action();
            } else if (harvest_action_ && action_step_<steps_.size() && view.observations
                && view.observations->confirmation.has_value(view.context,Time(2000),minimum_)) {
                phase_=Phase::acting;
                return emit(steps_.at(action_step_++));
            }
            return wait("Confirming planter outcome");
        }
        throw std::logic_error("Planter execution has already finished");
    }
    void step_completed(const View& view) override {
        if (view.completed_steps!=issued_ || acknowledged_+1!=issued_) throw std::logic_error("Unexpected planter operation completion");
        acknowledged_=issued_;
        if (phase_==Phase::initializing) {
            phase_=Phase::select; ready_for_travel_=true;
            started_=minimum_=view.context.now+Time(1); deadline_=view.context.now+Time(10000);
        } else if (phase_==Phase::arriving) {
            const auto seconds=(view.context.now-travel_started_).count()/1000.0;
            if (learn_travel_.value_or(false) && seconds>=1 && seconds<=1800) { controller_->record_travel(selected_.field,seconds); travel_.record(selected_.field,seconds); }
            phase_=Phase::locating;
            minimum_=view.context.now;
            deadline_=view.context.now+Time(10000);
        } else if (phase_==Phase::approaching) {
            begin_baseline(view);
        } else if (phase_==Phase::acting && !harvest_ && action_step_==1 && std::holds_alternative<DragInventoryItem>(steps_.front())) {
            minimum_=view.context.now+Time(1); deadline_=view.context.now+Time(10000);
        } else if (phase_==Phase::acting && (action_step_==steps_.size() || (harvest_action_ && action_step_==1))) {
            if (!harvest_action_ || action_step_==1) controller_->input_finished(controller_->ledger().pending->ticket,view.context);
            phase_=Phase::confirming; minimum_=view.context.now+Time(1); deadline_=view.context.now+Time(10000); publish(ExecutionStage::confirming);
        } else if (phase_==Phase::returning && plan_.conversion && plan_.conversion->enabled && (harvest_confirmed_ || plan_.conversion->every_return)) {
            phase_=Phase::checking_backpack; minimum_=view.context.now+Time(1); deadline_=view.context.now+Time(10000);
            publish(ExecutionStage::checking_backpack);
        }
    }
    void finished(State state) override {
        // An inventory search can fail before the placement dialog is opened.
        // Keep its slot free, but use the same backoff as other failed placements.
        if (state==State::failed && !harvest_ && phase_==Phase::acting && action_step_==1
            && !steps_.empty() && std::holds_alternative<DragInventoryItem>(steps_.front()))
            account_->defer_placements(wall());
        settle_action();
        release(); std::lock_guard lock(results_->mutex_); auto& report=results_->report_; report.state=state;
        if (controller_) { report.pending=controller_->ledger().pending;
            report.growing=std::count_if(controller_->ledger().plants.begin(),controller_->ledger().plants.end(),[](const Plant& plant) { return !plant.missing; }); }
    }
};
ExecutionRun program(ExecutionPlan plan,std::shared_ptr<const game::RouteNetwork> network,std::shared_ptr<Account> account,
    std::shared_ptr<const Source> source,std::shared_ptr<const Actions> actions,WallClock clock) {
    auto results=std::make_shared<ExecutionResults>();
    auto instance=std::make_unique<Execution>(std::move(plan),std::move(network),std::move(account),std::move(source),std::move(actions),std::move(clock),results);
    return {std::move(instance),std::move(results)};
}
}
