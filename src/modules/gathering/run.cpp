#include "game/actions/field/shift_lock.hpp"
#include <utility>
#include "run.hpp"
#include "strategy.hpp"
#include "game/detection/buffs.hpp"
#include "game/detection/field_boosts.hpp"
#include <algorithm>

namespace hive::features::gathering {
NavigationSource::NavigationSource(std::shared_ptr<const automation::NavigationPerception> perception,std::shared_ptr<const game::FieldGoalSource> goals)
    :perception_(std::move(perception)),goals_(std::move(goals)) {
    if (!perception_ || !goals_) throw std::invalid_argument("Gathering needs navigation observations and field goals");
}
Snapshot NavigationSource::read(const ObservationContext& context,const game::State& observations) const {
    auto navigation=perception_->read(context,observations);
    if (navigation.observations.conflicting_anchors) throw std::runtime_error("Conflicting location observations");
    Snapshot result; result.field.observation=context; result.field.location=std::move(navigation.observations.anchor);
    result.field.available=std::move(navigation.available); result.field.blocked_routes=std::move(navigation.blocked);
    result.goals=goals_->snapshot(context.now,context.generation); return result;
}
std::vector<game::FieldGoal> NavigationSource::goals(const ObservationContext& context,const game::State&) const {
    return goals_->snapshot(context.now,context.generation);
}
bool BalloonSchedule::due(Time now,Time interval) const {
    std::lock_guard lock(mutex_);
    return interval==Time::zero() || !completed_ || now<*completed_ || now-*completed_>=interval;
}
void BalloonSchedule::completed(Time now) {
    std::lock_guard lock(mutex_); completed_=now;
}
namespace {
TimeExtension extension(const Plan& plan,const FieldPlan& field) {
    if (field.extend_while.empty()) return {};
    return {field.extension_limit,[buffs=plan.buffs,boosts=plan.field_boosts,selected=field.extend_while,name=field.preference.field](const automation::View& view) {
        const auto read=[&](const auto& source) -> std::shared_ptr<const game::Detection> {
            const auto result=source?source->read():std::nullopt;
            if (!result || !result->error.empty() || result->context.generation!=view.context.generation
                || result->context.window.id!=view.context.window.id || result->context.window.process!=view.context.window.process
                || result->context.window.bounds!=view.context.window.bounds || result->context.now>view.context.now
                || view.context.now-result->context.now>Time(3000)) return {};
            return result->value;
        };
        if (std::find(selected.begin(),selected.end(),"Field boost")!=selected.end()) {
            if (const auto result=std::dynamic_pointer_cast<const game::FieldBoostDetection>(read(boosts));result && result->readable)
                for (const auto& boost:result->boosts) if (boost.field==name) return true;
        }
        if (std::any_of(selected.begin(),selected.end(),[](const auto& name) { return name!="Field boost"; }))
            if (const auto result=std::dynamic_pointer_cast<const game::BuffDetection>(read(buffs)))
                for (const auto& buff:result->buffs) if (buff.active==true && std::find(selected.begin(),selected.end(),buff.name)!=selected.end()) return true;
        return false;
    }};
}
automation::FollowPath offset_path(Point offset,double speed,bool reverse=false) {
    workflows::CheckpointPlan movement;
    const auto leg=[&](double tiles,Key positive,Key negative) {
        if (tiles) movement.path.push_back({std::abs(tiles),tiles>0?positive:negative,{}});
    };
    if (reverse) { leg(-offset.x,Key::right,Key::left); leg(-offset.y,Key::forward,Key::backward); }
    else { leg(offset.y,Key::forward,Key::backward); leg(offset.x,Key::right,Key::left); }
    movement.speed=speed; movement.attempts=0; movement.completion=workflows::CheckpointCompletion::movement;
    movement.timeout=Time(static_cast<Time::rep>(std::ceil((std::abs(offset.x)+std::abs(offset.y))*4000/speed)))+Time(5000);
    workflows::validate(movement); return {std::move(movement)};
}
}
Report Results::read() const { std::lock_guard lock(mutex_); return report_; }
class Gathering final : public automation::Program {
    Plan plan_;
    std::shared_ptr<const game::RouteNetwork> network_;
    std::shared_ptr<const game::FieldPolicy> policy_;
    std::shared_ptr<const Source> source_;
    std::vector<game::FieldPreference> preferences_;
    std::shared_ptr<Results> results_;
    std::size_t acknowledged_{};
    std::size_t consecutive_recoveries_{};
    enum class Stage { prepare_hive, hive_unlocked, select, arrived, positioned, setup, rotated, shift_enabled, gathered, shift_disabled, pattern_restored, restored, position_restored, prepared_return, returned, converted } stage_{Stage::prepare_hive};
    std::size_t selected_{}, next_setup_{}, completed_{}, expected_{}, rotation_{};
    Time last_{};
    bool gathered_{}, temporary_{},yielding_{},converting_balloon_{};
    std::optional<unsigned> pattern_heading_;
    std::string gather_end_;
    bool objective_complete_{};
    std::optional<game::MenuTab> tracking_menu_;
    game::FieldDecision choose(const automation::View& view) const {
        auto snapshot=source_->read(view.context,view.observations?*view.observations:game::State{});
        snapshot.field.route_position=view.route_position;
        const auto& c=snapshot.field.observation;
        if (c.generation!=view.context.generation || c.now!=view.context.now || c.window.id!=view.context.window.id
            || c.window.process!=view.context.window.process || c.window.bounds!=view.context.window.bounds)
            throw std::runtime_error("Gathering received a stale planning context");
        auto ordered=preferences_;
        for (auto& field:ordered) if (!field.enabled)
            field.enabled=std::any_of(snapshot.goals.begin(),snapshot.goals.end(),[&](const auto& goal) {
                return !goal.complete && goal.generation==c.generation && goal.expires>c.now && game::accepts(goal,field.field);
            });
        const auto& location=snapshot.field.location;
        const bool tracked=view.route_position && view.route_position->usable(c);
        const bool observed=location.has_value(c,Time(2000)) && (!tracked || location.captured>=view.route_position->context.now);
        const auto at=observed?location.value:tracked?view.route_position->location:std::string{};
        snapshot.field.current_field.reset();
        for (const auto& field : preferences_) if (at==field.location) snapshot.field.current_field=field.field;
        std::rotate(ordered.begin(),ordered.begin()+rotation_,ordered.end());
        return game::choose_field(*network_,ordered,snapshot.goals,snapshot.field,*policy_);
    }
    void phase(Phase phase) {
        std::lock_guard lock(results_->mutex_); results_->report_.phase=phase;
    }
    void selection(const game::FieldDecision& decision) {
        tracking_menu_.reset();
        for (const auto& goal:decision.contributors) if (!goal.complete && goal.menu) { tracking_menu_=goal.menu; break; }
        std::lock_guard lock(results_->mutex_);
        results_->report_.field=decision.field; results_->report_.reason=decision.reason; results_->report_.contributors=decision.contributors;
    }
    automation::Step select(const automation::View& view) {
        const auto decision=choose(view);
        selection(decision);
        if (!decision.field) throw std::runtime_error("No reachable field with fresh location evidence");
        for (std::size_t i=0;i<plan_.fields.size();++i) if (plan_.fields[i].preference.field==*decision.field) selected_=i;
        const auto& field=plan_.fields[selected_];
        temporary_=decision.reason!=game::FieldReason::fallback;
        next_setup_=0; gather_end_.clear();
        if (view.backpack_percent && *view.backpack_percent>=*field.pattern.backpack_limit) {
            gather_end_="Bag "+std::to_string(static_cast<int>(std::round(*view.backpack_percent)))+"% before gathering";
            gathered_=false; stage_=Stage::returned; phase(Phase::returning); return automation::Navigate{plan_.hive};
        }
        stage_=Stage::arrived; phase(Phase::travelling); return automation::Navigate{field.preference.location};
    }
public:
    Gathering(Plan plan,std::shared_ptr<const game::RouteNetwork> network,std::shared_ptr<const game::FieldPolicy> policy,std::shared_ptr<const Source> source,std::shared_ptr<Results> results)
        :plan_(std::move(plan)),network_(std::move(network)),policy_(std::move(policy)),source_(std::move(source)),results_(std::move(results)) {
        plan_.config.require_done=true;
        if (!network_ || !policy_ || !source_ || !network_->contains(plan_.hive) || plan_.fields.empty() || plan_.fields.size()>64
            || !plan_.balloon_schedule || plan_.balloon_interval<Time::zero() || plan_.balloon_interval>std::chrono::hours(24)
            || plan_.cycles<1 || plan_.cycles>10000 || plan_.max_recoveries>10 || !plan_.config.observe || plan_.config.backpack_limit
            || plan_.config.duration<Time(1000) || plan_.config.duration>std::chrono::hours(8)
            || !std::isfinite(plan_.config.speed) || plan_.config.speed<1 || plan_.config.speed>200)
            throw std::invalid_argument("Invalid gathering cycle configuration");
        results_->report_.target_cycles=plan_.cycles;
        std::set<std::string> names;
        for (auto& f : plan_.fields) {
            if (auto prepare=std::exchange(f.prepare,{})) {
                auto steps=prepare(); f.setup.insert(f.setup.end(),steps.begin(),steps.end());
            }
            if (f.rotation) {
                game::camera_actions(*f.rotation);
                if (f.rotation->key!=Key::rotate_left && f.rotation->key!=Key::rotate_right)
                    throw std::invalid_argument("Gathering rotation must be a yaw turn");
            }
            if (f.preference.field.empty() || f.preference.field.size()>80 || !names.insert(f.preference.field).second
                || !network_->contains(f.preference.location) || (f.pattern.legs.empty() && !f.strategy) || (!f.pattern.legs.empty() && f.strategy) || f.pattern.legs.size()>2048 || f.pattern.until
                || !f.pattern.backpack_limit || !std::isfinite(*f.pattern.backpack_limit) || *f.pattern.backpack_limit<1 || *f.pattern.backpack_limit>100
                || !std::isfinite(f.start_offset.x) || !std::isfinite(f.start_offset.y) || std::abs(f.start_offset.x)>20 || std::abs(f.start_offset.y)>20
                || f.pattern.duration<Time(1000) || f.pattern.duration>std::chrono::hours(8) || f.setup.size()>32)
                throw std::invalid_argument("Invalid gathering field plan");
            for (const auto& leg : f.pattern.legs) game::movement_slice(leg.leg,leg.leg.tiles,plan_.config.speed);
            for (const auto& step : f.setup) {
                if (const auto* camera=std::get_if<automation::MoveCamera>(&step)) {
                    game::camera_actions(camera->movement);
                } else if (const auto* heading=std::get_if<automation::SetHeading>(&step)) {
                    game::heading_move(0,heading->heading);
                } else if (const auto* reference=std::get_if<automation::ObserveHeading>(&step)) {
                    if (!reference->reference || reference->heading>7 || reference->timeout<Time(100) || reference->timeout>Time(10000))
                        throw std::invalid_argument("Invalid field camera reference");
                } else if (const auto* alignment=std::get_if<automation::ApproachLandmark>(&step)) {
                    workflows::validate(alignment->movement);
                    if (!alignment->source) throw std::invalid_argument("Field alignment needs observations");
                } else if (const auto* placement=std::get_if<automation::PlaceSprinkler>(&step)) {
                    workflows::validate(placement->plan);
                    if ((placement->plan.completion==workflows::SprinklerCompletion::observed && !placement->source) || placement->plan.field!=f.preference.field) throw std::invalid_argument("Sprinkler setup does not match the gathering field");
                } else if (const auto* path=std::get_if<automation::FollowPath>(&step)) {
                    workflows::validate(path->movement);
                    if (path->movement.completion!=workflows::CheckpointCompletion::movement)
                        throw std::invalid_argument("Field path cannot claim an observed arrival");
                } else if (!std::holds_alternative<automation::Hold>(step) && !std::holds_alternative<automation::AwaitText>(step)
                    && !std::holds_alternative<automation::ApproachPrompt>(step)) throw std::invalid_argument("Unsupported field preparation operation");
            }
            preferences_.push_back(f.preference);
        }
    }
    void observe(const automation::View& view) override {
        if (stage_!=Stage::gathered || yielding_) return;
        const auto selected=results_->read().contributors;
        const auto current=source_->goals(view.context,view.observations?*view.observations:game::State{});
        int priority=-1;
        for (const auto& goal:selected) if (!goal.complete) priority=std::max(priority,goal.priority);
        if (std::any_of(current.begin(),current.end(),[&](const auto& goal) {
            const bool retargeted=goal.priority>=priority && std::any_of(selected.begin(),selected.end(),[&](const auto& previous) {
                return previous.owner==goal.owner && previous.id==goal.id;
            });
            return goal.interrupt && !goal.complete && (goal.priority>priority || retargeted) && goal.generation==view.context.generation
                && goal.expires>view.context.now && !game::accepts(goal,plan_.fields[selected_].preference.field)
                && std::any_of(preferences_.begin(),preferences_.end(),[&](const auto& field) { return game::accepts(goal,field.field); });
        })) { yielding_=true; return; }
        if (selected.empty()) return;
        if (std::all_of(selected.begin(),selected.end(),[&](const auto& goal) {
            return std::any_of(current.begin(),current.end(),[&](const auto& now) {
                return now.owner==goal.owner && now.id==goal.id && now.complete
                    && now.generation==view.context.generation && now.expires>view.context.now;
            });
        })) { yielding_=true; objective_complete_=true; }
    }
    bool request_yield() override { if (stage_!=Stage::gathered) return false; yielding_=true; return true; }
    automation::RunConfig config() const override { return plan_.config; }
    std::optional<game::MenuTab> movement_menu() const override {
        return stage_<=Stage::gathered?tracking_menu_:std::nullopt;
    }
    std::string field() const override { return stage_==Stage::gathered?plan_.fields[selected_].preference.field:std::string{}; }
    ActivityStatus activity_status() const override {
        switch (stage_) {
        case Stage::prepare_hive: case Stage::hive_unlocked: case Stage::select: return {};
        case Stage::arrived: return {"Travelling",plan_.fields[selected_].preference.field};
        case Stage::positioned: case Stage::setup: case Stage::rotated: case Stage::shift_enabled:
            return {};
        case Stage::gathered: return {"Gathering",plan_.fields[selected_].preference.field};
        case Stage::converted: return {"Converting",converting_balloon_?"Backpack and hive balloon":"Backpack"};
        default: return {"Returning to hive",gather_end_};
        }
    }
    bool resume_after_reconnect(const automation::View& view) override { return prepare_recovery(view); }
    automation::ProgramFactory recovery(automation::Interruption reason,const automation::View& view) override {
        const auto factory=reason==automation::Interruption::death?plan_.recovery:
            (reason==automation::Interruption::lost_route || reason==automation::Interruption::lost_observations)?plan_.route_recovery:
            automation::ProgramFactory{};
        return factory && prepare_recovery(view)?factory:automation::ProgramFactory{};
    }
    bool prepare_recovery(const automation::View& view) {
        if (consecutive_recoveries_>=plan_.max_recoveries) return false;
        ++consecutive_recoveries_;
        {
            std::lock_guard lock(results_->mutex_);
            const auto completed=results_->report_.completed_cycles;
            if (completed>completed_ && !temporary_) rotation_=(selected_+1)%preferences_.size();
            completed_=completed; ++results_->report_.recoveries; results_->report_.phase=Phase::recovering;
        }
        expected_=acknowledged_=view.completed_steps; last_=view.elapsed;
        gathered_=false; converting_balloon_=false; pattern_heading_.reset(); next_setup_=0; stage_=Stage::converted;
        return true;
    }
    automation::Step next(const automation::View& view) override {
        if (view.completed_steps!=expected_ || view.elapsed<last_) throw std::logic_error("Gathering progress is out of order");
        ++expected_; last_=view.elapsed;
        { std::lock_guard lock(results_->mutex_); results_->report_.state=automation::State::running; }
        if (stage_==Stage::converted) {
            if (gathered_) { ++completed_; if (!temporary_) rotation_=(selected_+1)%preferences_.size(); }
            if (yielding_ || completed_>=plan_.cycles) return automation::Done{"Gathering cycles completed"};
            stage_=Stage::prepare_hive;
        }
        if (stage_==Stage::prepare_hive) {
            tracking_menu_.reset();
            for (const auto& goal:source_->goals(view.context,view.observations?*view.observations:game::State{}))
                if (!goal.complete && goal.menu && goal.generation==view.context.generation && goal.expires>view.context.now) {
                    tracking_menu_=goal.menu; break;
                }
            stage_=Stage::hive_unlocked;
            if (std::any_of(plan_.fields.begin(),plan_.fields.end(),[this](const auto& field) { return field.shift_lock.value_or(plan_.shift_lock); })) return automation::RunProgram{[] { return automation::set_shift_lock(false); }};
        }
        if (stage_==Stage::hive_unlocked) {
            stage_=Stage::select;
            if (plan_.hive_setup) { phase(Phase::preparing); return automation::RunProgram{plan_.hive_setup}; }
        }
        if (stage_==Stage::select) return select(view);
        if (stage_==Stage::arrived) {
            const auto decision=choose(view);
            selection(decision);
            if (!decision.field) throw std::runtime_error("Field prerequisites expired during travel");
            if (*decision.field!=plan_.fields[selected_].preference.field) return select(view);
            stage_=Stage::positioned;
            const auto offset=plan_.fields[selected_].start_offset;
            if (offset.x || offset.y) { phase(Phase::preparing); return offset_path(offset,plan_.config.speed); }
        }
        if (stage_==Stage::positioned) stage_=Stage::setup;
        if (stage_==Stage::setup) {
            const auto& field=plan_.fields[selected_];
            if (next_setup_<field.setup.size()) { phase(Phase::preparing); return field.setup[next_setup_++]; }
            stage_=Stage::rotated;
            if (field.rotation) {
                if (auto rotate=game::heading_move(0,game::turned_heading(0,*field.rotation))) {
                    rotate->press=field.rotation->press; rotate->release=field.rotation->release;
                    phase(Phase::preparing); return automation::MoveCamera{*rotate};
                }
            }
        }
        if (stage_==Stage::rotated) {
            stage_=Stage::shift_enabled;
            if (plan_.fields[selected_].shift_lock.value_or(plan_.shift_lock)) return automation::RunProgram{[] { return automation::set_shift_lock(true); }};
        }
        if (stage_==Stage::shift_enabled) {
            const auto& field=plan_.fields[selected_];
            gathered_=true; stage_=Stage::gathered; phase(Phase::gathering); pattern_heading_.reset(); gather_end_.clear(); objective_complete_=false;
            const auto hotbar=[this](const automation::View& current) {
                if (yielding_) return 0u;
                const auto goals=source_->goals(current.context,current.observations?*current.observations:game::State{});
                for (const auto& goal:goals) if (!goal.complete && goal.hotbar_slot && goal.expires>current.context.now
                    && goal.generation==current.context.generation && game::accepts(goal,plan_.fields[selected_].preference.field)) return goal.hotbar_slot;
                return 0u;
            };
            if (field.strategy) {
                const automation::RunConfig limits{field.pattern.duration,plan_.config.speed,field.pattern.backpack_limit,true};
                return automation::RunProgram{[this,create=field.strategy,between_cycles=field.pattern.between_cycles,limits,hotbar,extend=extension(plan_,field)] {
                    return bounded_pattern(create,limits,[this](unsigned heading) { pattern_heading_=heading; },between_cycles,[this] { return yielding_; },hotbar,extend);
                }};
            }
            auto pattern=field.pattern; pattern.activity=WorkActivity::gathering; pattern.hotbar=hotbar; pattern.yield=[this] { return yielding_; }; return pattern;
        }
        if (stage_==Stage::gathered) {
            stage_=Stage::shift_disabled;
            if (plan_.fields[selected_].shift_lock.value_or(plan_.shift_lock)) return automation::RunProgram{[] { return automation::set_shift_lock(false); }};
        }
        if (stage_==Stage::shift_disabled) {
            stage_=Stage::pattern_restored; phase(Phase::returning);
            if (plan_.fields[selected_].strategy) {
                if (!pattern_heading_) throw std::runtime_error("Pattern did not confirm its final facing");
                if (auto restore=game::heading_move(*pattern_heading_,0)) return automation::MoveCamera{*restore};
            }
        }
        if (stage_==Stage::pattern_restored) {
            stage_=Stage::restored; phase(Phase::returning);
            if (const auto& rotation=plan_.fields[selected_].rotation) {
                if (auto restore=game::heading_move(game::turned_heading(0,*rotation),0)) {
                    restore->press=rotation->press; restore->release=rotation->release;
                    return automation::MoveCamera{*restore};
                }
            }
        }
        if (stage_==Stage::restored) {
            stage_=Stage::position_restored;
            const auto& field=plan_.fields[selected_];
            if (!field.reset_return && (field.start_offset.x || field.start_offset.y)) return offset_path(field.start_offset,plan_.config.speed,true);
        }
        if (stage_==Stage::position_restored) {
            stage_=Stage::prepared_return; phase(Phase::returning);
            if (plan_.fields[selected_].return_setup) return automation::RunProgram{plan_.fields[selected_].return_setup};
        }
        if (stage_==Stage::prepared_return) { stage_=Stage::returned; phase(Phase::returning); return automation::Navigate{plan_.hive}; }
        if (stage_==Stage::returned) {
            stage_=Stage::converted; phase(Phase::converting);
            converting_balloon_=plan_.convert_balloon && plan_.balloon_schedule->due(view.context.now,plan_.balloon_interval);
            return automation::Convert{converting_balloon_,gathered_};
        }
        throw std::logic_error("Invalid gathering phase");
    }
    void step_completed(const automation::View& view) override {
        if (view.completed_steps!=expected_ || view.completed_steps<=acknowledged_ || view.context.now<Time::zero())
            throw std::logic_error("Unexpected gathering completion");
        acknowledged_=view.completed_steps;
        if (stage_==Stage::gathered && view.completed) {
            const auto& end=*view.completed;
            const auto seconds=end.elapsed.count()/1000;
            const auto elapsed=std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60);
            const auto reason=objective_complete_?"Objective complete":end.message;
            gather_end_=plan_.fields[selected_].preference.field+" · "+elapsed+" · "+reason;
            if (end.backpack_percent) gather_end_+=" · Bag "+std::to_string(static_cast<int>(std::round(*end.backpack_percent)))+"%";
        }
        if (stage_==Stage::converted) {
            if (converting_balloon_) { plan_.balloon_schedule->completed(view.context.now); converting_balloon_=false; }
            consecutive_recoveries_=0;
            std::lock_guard lock(results_->mutex_);
            results_->report_.converted_at=view.context.now;
            if (gathered_) ++results_->report_.completed_cycles;
        }
    }
    void finished(automation::State state) override {
        std::lock_guard lock(results_->mutex_); results_->report_.state=state;
        if (state==automation::State::completed) results_->report_.phase=Phase::finished;
    }
};
Run run(Plan plan,std::shared_ptr<const game::RouteNetwork> network,
    std::shared_ptr<const game::FieldPolicy> policy,std::shared_ptr<const Source> source) {
    auto results=std::make_shared<Results>();
    auto program=std::make_unique<Gathering>(std::move(plan),std::move(network),std::move(policy),std::move(source),results);
    return {std::move(program),std::move(results)};
}
std::unique_ptr<automation::Program> program(Plan plan,std::shared_ptr<const game::RouteNetwork> network,
    std::shared_ptr<const game::FieldPolicy> policy,std::shared_ptr<const Source> source) {
    return run(std::move(plan),std::move(network),std::move(policy),std::move(source)).program;
}
}
