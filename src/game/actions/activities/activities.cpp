#include "core/runtime/runner.hpp"
#include "game/detection/menus.hpp"
#include "core/runtime/guards.hpp"
#include "game/data/stations.hpp"
#include <algorithm>
#include <utility>

namespace hive::automation {
bool Runner::observes_interaction() const {
    return config_.observe_interaction || (step_ && (std::holds_alternative<UseStation>(*step_) || std::holds_alternative<Navigate>(*step_)))
        || (recovery_ && recovery_->observes_interaction()) || (child_ && child_->observes_interaction())
        || (pattern_ && pattern_->observes_interaction());
}
bool Runner::observes_game() const {
    return config_.observe || config_.backpack_limit
        || (recovery_ && recovery_->observes_game()) || (child_ && child_->observes_game()) || (pattern_ && pattern_->observes_game());
}
std::vector<game::FixedLabelQuery> Runner::fixed_labels() const {
    if (recovery_) return recovery_->fixed_labels();
    if (progress_.state!=State::running || awaiting_window_) return {};
    auto queries=program_->fixed_labels();
    for (const auto* child:{child_.get(),pattern_.get()}) if (child) {
        const auto requested=child->fixed_labels(); queries.insert(queries.end(),requested.begin(),requested.end());
    }
    return queries;
}
std::vector<Rect> Runner::text_regions() const {
    if (recovery_) return recovery_->text_regions();
    if (progress_.state!=State::running || awaiting_window_) return {};
    auto regions=program_->text_regions();
    if (guard_) {
        const auto requested=guard_->text_regions(); regions.insert(regions.end(),requested.begin(),requested.end());
    }
    for (const auto* child:{child_.get(),pattern_.get()}) if (child) {
        const auto requested=child->text_regions(); regions.insert(regions.end(),requested.begin(),requested.end());
    }
    if (step_) std::visit([&](const auto& step) {
        using T=std::decay_t<decltype(step)>;
        if constexpr (std::is_same_v<T,AwaitText>) regions.push_back(step.target.region);
        else if constexpr (std::is_same_v<T,ApproachPrompt>) regions.push_back(step.prompt.region);
        else if constexpr (std::is_same_v<T,UsePrompt>) { regions.push_back(step.prompt.region); regions.push_back(step.result.region); }
        else if constexpr (std::is_same_v<T,UseStation>) {
            const auto* definition=game::station_definition(step.name);
            if (!definition || definition->interaction!=game::StationInteraction::activate) regions.push_back(game::station_target(step.name).region);
        }
        else if constexpr (std::is_same_v<T,Pattern>) { if (step.until) regions.push_back(step.until->region); }
        else if constexpr (std::is_same_v<T,AnswerDialog>) { if (step.plan.read_question) regions.push_back(step.plan.question.region); }
        else if constexpr (std::is_same_v<T,Convert>) { if (!conversion_ || conversion_->needs_prompt()) regions.push_back(game::hive_proximity_prompt().region); }
        else if constexpr (std::is_same_v<T,PlaceSprinkler>) { if (step.plan.completion==workflows::SprinklerCompletion::attempted) regions.push_back({.45,.4,.55,.6}); }
    },*step_);
    return regions;
}
namespace {
class PatternGuard final : public RunGuard {
    std::unique_ptr<RunGuard> observations_;
    Time duration_;
    std::function<bool()> yield_;
public:
    explicit PatternGuard(RunConfig config,std::function<bool()> yield):observations_(observation_guard(config)),duration_(config.duration),yield_(std::move(yield)) {}
    GuardDecision evaluate(const View& view,Time started) override {
        const auto decision=observations_->evaluate(view,started);
        if (decision.state!=GuardState::ready) return decision;
        if (yield_ && yield_()) return {GuardState::completed,"Returning for other work"};
        if (view.elapsed>=duration_) return {GuardState::completed,"Pattern duration reached"};
        return {};
    }
};
class PatternProgram final : public Program {
    Pattern pattern_;
    double speed_;
    std::size_t leg_{};
    Time corrected_{};
    bool correcting_{};
    Time used_slot_{Time(-2000)};
public:
    PatternProgram(Pattern pattern,double speed):pattern_(std::move(pattern)),speed_(speed) {
        if (pattern_.legs.empty() || pattern_.legs.size()>2048) throw std::invalid_argument("Pattern needs 1–2048 legs");
        for (const auto& leg : pattern_.legs) game::movement_slice(leg.leg,leg.leg.tiles,speed_);
    }
    WorkActivity activity() const noexcept override { return pattern_.activity; }
    RunConfig config() const override { return {pattern_.duration,speed_,pattern_.backpack_limit,true}; }
    std::unique_ptr<RunGuard> guard() const override { return std::make_unique<PatternGuard>(config(),pattern_.yield); }
    Step next(const View& view) override {
        if (pattern_.hotbar && view.context.now-used_slot_>=Time(2000)) {
            const auto slot=pattern_.hotbar(view);
            if (slot>=1 && slot<=7) { used_slot_=view.context.now; return Hold{{Key(int(Key::hotbar_1)+slot-1)},Time(50)}; }
        }
        if (leg_==pattern_.legs.size()) {
            leg_=0;
            if (pattern_.between_cycles && view.elapsed-corrected_>=Time(5000)) {
                correcting_=true; return RunProgram{pattern_.between_cycles};
            }
        }
        return pattern_.legs.at(leg_++);
    }
    void step_completed(const View& view) override {
        if (correcting_) { corrected_=view.elapsed; correcting_=false; }
    }
};
}
void Runner::complete_step(const ObservationContext& c,const game::State& s) {
    const auto completed=child_?std::optional(child_->progress()):pattern_?std::optional(pattern_->progress()):std::nullopt;
    child_.reset();
    pattern_.reset(); interaction_.reset(); conversion_.reset(); checkpoint_.reset(); navigation_.reset(); sprinkler_.reset(); inventory_.reset(); dialog_.reset();
    if (travel_pointer_) { travel_pointer_->cancel(scope_); tasks_.cancel(scope_); }
    travel_inventory_.reset(); travel_pointer_.reset(); navigation_input_.clear();
    step_.reset();
    progress_.route_execution.reset();
    if (!progress_.route_position && !progress_.estimated_heading) { tasks_.release(scope_); owned_=false; }
    ++progress_.steps;
    program_->step_completed({progress_.elapsed,progress_.steps,progress_.backpack_percent,c,&s,progress_.estimated_heading,progress_.route_position,{},completed?&*completed:nullptr});
}
void Runner::checkpoint_failed(const ObservationContext& c,const game::State& s) {
    const auto failure=checkpoint_->failure();
    const auto message=checkpoint_->reason();
    if (failure==workflows::CheckpointFailure::missing || failure==workflows::CheckpointFailure::timeout)
        interrupt(Interruption::lost_route,c,s,message);
    else finish(State::failed,message);
}
std::optional<game::MenuTab> Runner::movement_menu() const {
    if (auto menu=program_->movement_menu()) return menu;
    return parent_menu_?parent_menu_():std::nullopt;
}
bool Runner::prepare_menu(const ObservationContext& c,const game::State& s) {
    if (!permissions_.contains(Permission::interact) || checkpoint_ || navigation_ || sprinkler_) return false;
    if (const auto* travel=std::get_if<Navigate>(&*step_); travel && travel->inventory_search) return false;
    const auto* hold=std::get_if<Hold>(&*step_);
    const bool walking_hold=hold && std::any_of(hold->keys.begin(),hold->keys.end(),[](auto key) {
        return key==Key::forward || key==Key::backward || key==Key::left || key==Key::right;
    });
    const bool moving=walking_hold || std::holds_alternative<Walk>(*step_) || std::holds_alternative<Navigate>(*step_)
        || std::holds_alternative<MoveCamera>(*step_) || std::holds_alternative<SetHeading>(*step_)
        || std::holds_alternative<FollowPath>(*step_)
        || std::holds_alternative<ApproachLandmark>(*step_) || std::holds_alternative<ApproachPrompt>(*step_);
    if (!moving) return false;
    if (!menu_pending_.empty()) {
        for (const auto& result:tasks_.results(scope_)) if (result.id==menu_pending_) {
            menu_pending_.clear();
            if (result.outcome!=TaskOutcome::executed) throw std::runtime_error("Menu cleanup interrupted");
            menu_after_=result.finished+Time(250);
        }
        if (!menu_pending_.empty()) return true;
    }
    if (c.now<menu_after_) return true;
    std::optional<Point> target;
    // Camera keys such as Page Up can scroll an open quest/inventory panel.
    // Tracking menus may reopen for movement after the camera adjustment.
    const auto required=std::holds_alternative<MoveCamera>(*step_) || std::holds_alternative<SetHeading>(*step_)
        ?std::optional<game::MenuTab>{}:movement_menu();
    const bool fresh=required?s.menus.has_value(c,Time(1500),menu_after_):s.open_menu.usable(c,Time(1500),menu_after_);
    if (!fresh && menu_attempts_) {
        if (c.now>=menu_after_+Time(5000)) throw ObservationUnavailable("Game menu change could not be confirmed");
        return true;
    }
    if (required) {
        if (!fresh) return false;
        for (const auto& menu:s.menus.value) if (menu.tab==*required) {
            if (menu.selected==true) { menu_attempts_=0; return false; }
            if (menu.selected==false) target=menu.center();
            break;
        }
    } else if (fresh && s.open_menu.knowledge==Knowledge::present) target=s.open_menu.value;
    else if (fresh && s.open_menu.knowledge==Knowledge::absent) menu_attempts_=0;
    if (!target) return false;
    const auto point=*target;
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x<0 || point.y<0 || point.x>=1 || point.y>=1)
        throw std::runtime_error("Invalid menu control position");
    if (menu_attempts_>=3) throw ObservationUnavailable("Game menu did not respond after three clicks");
    if (!owned_) { if (!tasks_.acquire(scope_)) return true; owned_=true; }
    ++menu_attempts_; progress_.message=required?"Opening tracking menu":"Closing game menu";
    const auto rest=game::pointer_rest(c,s);
    menu_pending_=tasks_.submit(scope_,TaskClass::interaction,
        {{progress_.message,{Key::primary},Time(50),point},{"Move off menu",{},Time(50),rest}},Time(2000));
    return true;
}
bool Runner::activity(const ObservationContext& c,const game::State& s) {
    if (const auto* report=std::get_if<ReportActivity>(&*step_)) {
        if (activity_sink_) { try { activity_sink_(*report); } catch (...) {} }
        complete_step(c,s); return true;
    }
    if (std::holds_alternative<EndCycle>(*step_)) { complete_step(c,s); return true; }
    if (std::holds_alternative<Walk>(*step_) || std::holds_alternative<Hold>(*step_)) return false;
    if (const auto* operation=std::get_if<RunProgram>(&*step_)) {
        if (!operation->create) throw std::invalid_argument("Missing workflow factory");
        if (!child_) {
            if (depth_>=8) throw std::invalid_argument("Workflow nesting exceeds eight levels");
            child_=std::make_unique<Runner>(operation->create(),permissions_,static_cast<TaskPort&>(*this),scope_,navigation_services_,depth_+1); child_->configure_conversion(hive_conversion_); child_->configure_statistics(recovery_counts_); child_->configure_reporting(activity_sink_);
            child_->parent_menu_=[this] { return movement_menu(); };
            child_->start(c);
            child_->progress_.route_position=std::move(progress_.route_position); progress_.route_position.reset();
            child_->progress_.estimated_heading=std::exchange(progress_.estimated_heading,{});
        }
        child_->update(c,s); const auto result=child_->progress(); progress_.message=result.message;
        progress_.route_execution=result.route_execution;
        if (result.state==State::completed || (operation->return_result && (result.state==State::failed || result.state==State::deferred))) {
            progress_.route_position=result.state==State::completed?result.route_position:std::nullopt;
            progress_.estimated_heading=result.state==State::completed?result.estimated_heading:std::nullopt;
            complete_step(c,s);
        } else if (result.state==State::deferred) finish(State::deferred,result.message);
        else if (result.state==State::failed && result.interruption) interrupt(*result.interruption,c,s,result.message);
        else if (result.state==State::failed || result.state==State::stopped || result.state==State::paused)
            finish(State::failed,result.message);
        return true;
    }
    if (const auto* heading=std::get_if<AdoptHeading>(&*step_)) {
        if (!permissions_.contains(Permission::movement) || heading->heading>7)
            throw std::invalid_argument("Invalid camera heading or missing movement permission");
        if (!heading->reference.has_value(c,Time(60000)) || !heading->reference.value)
            throw ObservationUnavailable("Known camera heading was not confirmed in this game view");
        progress_.estimated_heading=heading->heading;
        complete_step(c,s); return true;
    }
    if (const auto* heading=std::get_if<ObserveHeading>(&*step_)) {
        if (!permissions_.contains(Permission::movement) || !heading->reference || heading->heading>7
            || heading->timeout<Time(100) || heading->timeout>Time(10000)) throw std::invalid_argument("Invalid camera heading reference");
        if (c.now-step_started_>=heading->timeout) throw ObservationUnavailable("Camera heading reference was not confirmed");
        if (!baseline_clear_) {
            if (!tasks_.acquire(scope_)) { progress_.message="Waiting for camera control"; return true; }
            owned_=true; baseline_clear_=true; confirmation_frame_=c.now;
        }
        progress_.message="Confirming camera heading";
        const auto value=heading->reference->read(c);
        if (value.has_value(c,Time(2000),step_started_) && value.value && value.captured>confirmation_frame_) {
            progress_.estimated_heading=heading->heading; complete_step(c,s);
        }
        return true;
    }
    if (const auto* heading=std::get_if<SetHeading>(&*step_)) {
        if (!permissions_.contains(Permission::movement)) throw std::runtime_error("Camera movement permission was not granted");
        if (!progress_.estimated_heading) throw ObservationUnavailable("Camera heading must be established from a reference first");
        const auto move=game::heading_move(*progress_.estimated_heading,heading->heading);
        if (!move) { complete_step(c,s); return true; }
        step_=MoveCamera{*move};
    }
    if (const auto* camera=std::get_if<MoveCamera>(&*step_)) {
        if (!permissions_.contains(Permission::movement)) throw std::runtime_error("Camera movement permission was not granted");
        auto actions=game::camera_actions(camera->movement);
        if (!tasks_.acquire(scope_)) {
            if (progress_.estimated_heading) throw ObservationUnavailable("Camera control was lost; re-establish its heading");
            progress_.message="Waiting for game control"; return true;
        }
        owned_=true; progress_.message="Adjusting camera";
        pending_heading_=progress_.estimated_heading?std::optional<unsigned>(game::turned_heading(*progress_.estimated_heading,camera->movement)):std::nullopt;
        progress_.estimated_heading.reset();
        hold_left_=Time::zero();
        for (const auto& action : actions) hold_left_+=action.duration;
        const auto lifetime=input_lifetime(actions);
        pending_=tasks_.submit(scope_,TaskClass::activity,std::move(actions),lifetime);
        return true;
    }
    if (const auto* key=std::get_if<TapMenuKey>(&*step_)) {
        if (!permissions_.contains(Permission::interact) || !menu_key(key->key) || key->label.empty() || key->label.size()>120)
            throw std::runtime_error("Menu shortcut requires interaction permission");
        if (!tasks_.acquire(scope_)) return true;
        owned_=true; progress_.message=key->label;
        // Keep a brief pause after the tap; the program observes menu readiness.
        Action action{key->label,{},Time(50),{}};
        action.tap=key->key;
        pending_=tasks_.submit(scope_,TaskClass::interaction,{action},Time(1000));
        return true;
    }
    if (const auto* number=std::get_if<EnterNumber>(&*step_)) {
        const auto point=number->target.value;
        if (!permissions_.contains(Permission::interact) || !number->target.has_value(c,Time(2000),context_.now)
            || !std::isfinite(point.x) || !std::isfinite(point.y) || point.x<0 || point.y<0 || point.x>=1 || point.y>=1 || number->value>1000000)
            throw std::runtime_error("Number entry needs a fresh observed input and interaction permission");
        if (!tasks_.acquire(scope_)) return true;
        owned_=true; progress_.message="Entering amount";
        std::vector<Action> actions{{"Focus amount",{Key::primary},Time(100),point},{"Release pointer",{},Time(100),{}},
            {"Select amount",{Key::select_all},Time(100),{}},{"Release selection",{},Time(100),{}}};
        for (char digit:std::to_string(number->value)) {
            const auto key=digit=='0'?Key::digit_0:digit=='8'?Key::digit_8:digit=='9'?Key::digit_9:Key(int(Key::hotbar_1)+digit-'1');
            actions.push_back({"Enter digit",{key},Time(100),{}}); actions.push_back({"Release digit",{},Time(100),{}});
        }
        hold_left_=Time(actions.size()*100);
        const auto lifetime=input_lifetime(actions);
        pending_=tasks_.submit(scope_,TaskClass::interaction,std::move(actions),lifetime); return true;
    }
    if (const auto* control=std::get_if<UseControl>(&*step_)) {
        const auto point=control->target.value;
        const auto valid_point=[](Point p) { return std::isfinite(p.x) && std::isfinite(p.y) && p.x>=0 && p.y>=0 && p.x<1 && p.y<1; };
        if (!permissions_.contains(Permission::interact)) throw std::runtime_error("UI interaction permission was not granted");
        if (!control->target.has_value(c,Time(2000),context_.now) || !valid_point(point)
            || (control->rest && !valid_point(*control->rest)) || control->scroll<-4 || control->scroll>4
            || control->label.empty() || control->label.size()>120) throw std::runtime_error("UI control observation is invalid or stale");
        if (!tasks_.acquire(scope_)) { progress_.message="Waiting for game control"; return true; }
        owned_=true; progress_.message=control->label;
        Action move{control->label,{},Time(40),point};
        Action input{control->label,control->scroll?std::vector<Key>{}:std::vector<Key>{Key::primary},Time(60),control->scroll?std::optional(point):std::nullopt,control->scroll};
        hold_left_=Time(100);
        std::vector<Action> actions{move,input};
        if (control->rest || !control->scroll) {
            actions.push_back({control->label,{},Time(40),control->rest.value_or(game::pointer_rest(c,s))}); hold_left_+=Time(40);
        }
        pending_=tasks_.submit(scope_,TaskClass::interaction,std::move(actions),Time(1000));
        return true;
    }
    if (const auto* answer=std::get_if<AnswerDialog>(&*step_)) {
        if (!permissions_.contains(Permission::interact)) throw std::runtime_error("Dialog interaction permission was not granted");
        if (!dialog_) { dialog_.emplace(answer->plan); dialog_->start(c); }
        dialog_->update(c,s,*this,scope_); progress_.message=dialog_->reason();
        if (dialog_->stage()==workflows::DialogStage::completed) complete_step(c,s);
        else if (dialog_->stage()==workflows::DialogStage::failed) {
            if (dialog_->timed_out()) interrupt(Interruption::lost_observations,c,s,dialog_->reason());
            else finish(State::failed,dialog_->reason());
        }
        return true;
    }
    if (const auto* item=std::get_if<InspectInventoryItem>(&*step_)) {
        if (!permissions_.contains(Permission::interact) || !item->source || !item->result)
            throw std::runtime_error("Inventory inspection requires permission, observations and a result");
        if (!inventory_) {
            *item->result={}; workflows::InventoryDragPlan plan{item->names,{}}; plan.inspect_only=true; plan.current_page_only=item->current_page_only;
            plan.start_at_bottom=item->start_at_bottom;
            plan.require_quantity=item->quantity;
            if (plan.current_page_only) plan.timeout=Time(5000);
            inventory_.emplace(std::move(plan)); inventory_->start(c);
        }
        if (item->quantity) item->source->request_quantities(c,item->names);
        inventory_->update(c,s,item->source->read(c,s),tasks_,scope_); progress_.message=inventory_->reason();
        if (inventory_->stage()==workflows::InventoryStage::completed) { *item->result=inventory_->match(); complete_step(c,s); }
        else if (inventory_->stage()==workflows::InventoryStage::failed || inventory_->stage()==workflows::InventoryStage::stopped)
            finish(State::failed,inventory_->reason());
        return true;
    }
    if (const auto* item=std::get_if<DragInventoryItem>(&*step_)) {
        if (!permissions_.contains(Permission::interact) || !item->source) throw std::runtime_error("Inventory interaction requires permission and observations");
        if (!inventory_) {
            auto plan=item->plan; plan.allow_absent=bool(item->result);
            inventory_.emplace(std::move(plan)); inventory_->start(c);
        }
        inventory_->update(c,s,item->source->read(c,s),tasks_,scope_); progress_.message=inventory_->reason();
        if (inventory_->stage()==workflows::InventoryStage::completed) {
            if (item->result) *item->result=inventory_->match();
            complete_step(c,s);
        }
        else if (inventory_->stage()==workflows::InventoryStage::failed || inventory_->stage()==workflows::InventoryStage::stopped) finish(State::failed,inventory_->reason());
        return true;
    }
    if (const auto* wait=std::get_if<Wait>(&*step_)) {
        if (wait->duration<Time(1) || wait->duration>Time(1000) || wait->reason.size()>160)
            throw std::invalid_argument("Invalid observation wait");
        if (!progress_.estimated_heading && !progress_.route_position) { tasks_.release(scope_); owned_=false; }
        progress_.message=wait->reason;
        if (c.now-step_started_>=wait->duration) complete_step(c,s);
        return true;
    }
    if (const auto* placement=std::get_if<PlaceSprinkler>(&*step_)) {
        if (!permissions_.contains(Permission::interact)
            || ((placement->plan.jump>Time::zero() || placement->plan.completion==workflows::SprinklerCompletion::attempted) && !permissions_.contains(Permission::movement)))
            throw std::runtime_error("Sprinkler placement permissions were not granted");
        if (placement->plan.completion==workflows::SprinklerCompletion::observed && !placement->source)
            throw std::runtime_error("Sprinkler observations are not configured");
        if (!sprinkler_) { sprinkler_.emplace(placement->plan); sprinkler_->start(c); }
        sprinkler_->update(c,s,placement->source?placement->source->read(c,s):Observation<game::SprinklerSite>{},tasks_,scope_); progress_.message=sprinkler_->reason();
        if (sprinkler_->stage()==workflows::SprinklerStage::completed) complete_step(c,s);
        else if (sprinkler_->stage()==workflows::SprinklerStage::failed || sprinkler_->stage()==workflows::SprinklerStage::stopped) finish(State::failed,sprinkler_->reason());
        return true;
    }
    if (const auto* path=std::get_if<FollowPath>(&*step_)) {
        if (!permissions_.contains(Permission::movement)) throw std::runtime_error("Path movement permission was not granted");
        if (path->movement.completion!=workflows::CheckpointCompletion::movement)
            throw std::invalid_argument("Path movement cannot claim a detected result");
        if (!checkpoint_) { checkpoint_.emplace(path->movement); checkpoint_->start(c,scope_); }
        progress_.message="Following travel path";
        checkpoint_->update(c,{},tasks_,s.movement_speed);
        if (checkpoint_->stage()==workflows::CheckpointStage::completed) complete_step(c,s);
        else if (checkpoint_->stage()==workflows::CheckpointStage::failed) checkpoint_failed(c,s);
        return true;
    }
    if (const auto* approach=std::get_if<ApproachLandmark>(&*step_)) {
        if (!permissions_.contains(Permission::movement) || !approach->source)
            throw std::runtime_error("Visual checkpoints require movement permission and observations");
        if (approach->movement.completion==workflows::CheckpointCompletion::launch && !permissions_.contains(Permission::interact))
            throw std::runtime_error("Transport interaction permission was not granted");
        if (!checkpoint_) { checkpoint_.emplace(approach->movement); checkpoint_->start(c,scope_); }
        progress_.message="Aligning with "+approach->movement.detector;
        checkpoint_->update(c,approach->source->read(c),tasks_,s.movement_speed);
        if (checkpoint_->stage()==workflows::CheckpointStage::completed) complete_step(c,s);
        else if (checkpoint_->stage()==workflows::CheckpointStage::failed) checkpoint_failed(c,s);
        return true;
    }
    if (auto* wait=std::get_if<AwaitText>(&*step_)) {
        game::validate(wait->target);
        if (wait->timeout<Time(100) || wait->timeout>Time(60000)) throw std::invalid_argument("Invalid observation timeout");
        progress_.message="Confirming "+wait->target.words;
        if (s.text.has_value(c,Time(2000),step_started_) && s.text.captured>confirmation_frame_) {
            confirmation_frame_=s.text.captured;
            if (game::text_matches(c,s,wait->target,step_started_)) { complete_step(c,s); return true; }
        }
        if (c.now-step_started_>=wait->timeout) interrupt(Interruption::lost_observations,c,s,"Could not confirm "+wait->target.words);
        return true;
    }
    if (auto* destination=std::get_if<Navigate>(&*step_)) {
        if (!permissions_.contains(Permission::movement)) throw std::runtime_error("Navigation movement permission was not granted");
        if (!navigation_services_.routes || !navigation_services_.perception) throw std::runtime_error("Calibrated navigation is not configured");
        if (!navigation_) {
            if (destination->inventory_search) {
                const auto& item=*destination->inventory_search;
                if (!permissions_.contains(Permission::interact) || !item.source || !item.result)
                    throw std::runtime_error("Travel inventory search requires interaction permission and observations");
                *item.result={};
                workflows::InventoryDragPlan plan{item.names,{}}; plan.inspect_only=true;
                plan.start_at_bottom=item.start_at_bottom;
                plan.require_quantity=item.quantity;
                travel_inventory_.emplace(std::move(plan)); travel_inventory_->start(c);
                travel_pointer_=std::make_shared<PointerTasks>(scope_);
            }
            navigation_.emplace(navigation_services_.routes,static_cast<TaskPort&>(*this),scope_);
            navigation_->start(destination->destination,c,std::move(progress_.route_position)); progress_.route_position.reset();
        }
        if (travel_inventory_) {
            const auto& item=*destination->inventory_search;
            travel_pointer_->clock(c.now);
            if (item.quantity) item.source->request_quantities(c,item.names);
            travel_inventory_->update(c,s,item.source->read(c,s),*travel_pointer_,scope_);
            if (travel_inventory_->stage()==workflows::InventoryStage::completed) *item.result=travel_inventory_->match();
            if (travel_inventory_->stage()==workflows::InventoryStage::completed || travel_inventory_->stage()==workflows::InventoryStage::failed
                || travel_inventory_->stage()==workflows::InventoryStage::stopped) travel_inventory_.reset();
        }
        auto view=navigation_services_.perception->read(c,s);
        view.observations.movement_speed=s.movement_speed; view.observations.alive=s.alive; view.observations.connection=s.connection;
        navigation_->update(c,view.observations,view.available,view.blocked);
        progress_.route_execution=navigation_->execution();
        const auto status=navigation_->status(); progress_.message=status.reason;
        if (status.stage==workflows::TravelStage::arrived) {
            if (travel_pointer_ && travel_pointer_->engaged()) return true;
            progress_.route_position=navigation_->position();
            complete_step(c,s);
        }
        else if (status.stage==workflows::TravelStage::failed && (status.failure==workflows::TravelFailure::location
            || status.failure==workflows::TravelFailure::timeout || status.failure==workflows::TravelFailure::exhausted
            || status.failure==workflows::TravelFailure::conflict))
            interrupt(Interruption::lost_route,c,s,status.reason);
        else if (status.stage==workflows::TravelStage::failed || status.stage==workflows::TravelStage::stopped) finish(State::failed,status.reason);
        return true;
    }
    if (auto* approach=std::get_if<ApproachPrompt>(&*step_)) {
        if (!permissions_.contains(Permission::movement)) throw std::runtime_error("Checkpoint movement permission was not granted");
        if (approach->movement.completion==workflows::CheckpointCompletion::launch && !permissions_.contains(Permission::interact))
            throw std::runtime_error("Transport interaction permission was not granted");
        if (!checkpoint_) {
            game::validate(approach->prompt);
            checkpoint_.emplace(approach->movement); checkpoint_->start(c,scope_);
        }
        progress_.message="Approaching "+approach->movement.detector;
        checkpoint_->update(c,game::read_prompt_landmark(c,s.text,approach->prompt),tasks_,s.movement_speed);
        if (checkpoint_->stage()==workflows::CheckpointStage::completed) complete_step(c,s);
        else if (checkpoint_->stage()==workflows::CheckpointStage::failed) checkpoint_failed(c,s);
        return true;
    }
    if (auto* prompt=std::get_if<UsePrompt>(&*step_)) {
        if (!permissions_.contains(Permission::interact)) throw std::runtime_error("Prompt interaction permission was not granted");
        if (!interaction_) {
            interaction_.emplace(prompt->prompt,prompt->result,prompt->timeout,prompt->accept_existing_result);
            interaction_->start(c);
        }
        progress_.message="Confirming interaction"; interaction_->update(c,s,tasks_,scope_);
        if (interaction_->stage()==workflows::InteractionStage::completed) complete_step(c,s);
        else if (interaction_->stage()==workflows::InteractionStage::failed) interrupt(Interruption::lost_observations,c,s,interaction_->reason());
        return true;
    }
    if (auto* station=std::get_if<UseStation>(&*step_)) {
        if (!permissions_.contains(Permission::interact)) throw std::runtime_error("Station interaction permission was not granted");
        if (!interaction_) {
            const auto* definition=game::station_definition(station->name);
            if (definition && definition->interaction==game::StationInteraction::activate) interaction_.emplace(station->timeout);
            else interaction_.emplace(game::station_target(station->name),game::station_target(station->name,true),station->timeout);
            interaction_->start(c);
        }
        progress_.message="Using "+station->name; interaction_->update(c,s,tasks_,scope_);
        if (interaction_->stage()==workflows::InteractionStage::completed) complete_step(c,s);
        else if (interaction_->stage()==workflows::InteractionStage::failed) interrupt(Interruption::lost_observations,c,s,interaction_->reason());
        return true;
    }
    if (std::holds_alternative<Convert>(*step_)) {
        if (!permissions_.contains(Permission::interact)) throw std::runtime_error("Conversion permission was not granted");
        if (!conversion_) {
            const auto& request=std::get<Convert>(*step_);
            workflows::ConversionConfig config; config.balloon=hive_conversion_?hive_conversion_->due(c,request.after_gather):request.balloon;
            config.balloon_with_backpack=hive_conversion_ && hive_conversion_->with_backpack();
            if (config.balloon) config.total_timeout=std::chrono::minutes(15);
            conversion_.emplace(config); conversion_->start(c);
        }
        progress_.message="Converting at hive"; conversion_->update(c,s,tasks_,scope_);
        if (conversion_->stage()==workflows::ConversionStage::completed) {
            if(hive_conversion_ && conversion_->converts_balloon()) hive_conversion_->completed(c);
            complete_step(c,s);
        }
        else if (conversion_->stage()==workflows::ConversionStage::recovery_required)
            interrupt(Interruption::lost_observations,c,s,conversion_->reason());
        return true;
    }
    if (auto* pattern=std::get_if<Pattern>(&*step_)) {
        if (pattern->until && pattern->outcome) throw std::invalid_argument("Pattern needs one outcome source");
        if (pattern->until || pattern->outcome) {
            if (!baseline_clear_ && pattern->outcome_baseline && pattern->outcome_baseline->usable(c,Time(2000))
                && !pattern->outcome_baseline->value && pattern->outcome_baseline->captured<=step_started_) {
                baseline_clear_=true;
                confirmation_frame_=std::max(confirmation_frame_,pattern->outcome_baseline->captured);
            }
            const auto result=pattern->outcome?pattern->outcome->read(c):game::read_text_target(c,s,*pattern->until,step_started_);
            if (result.usable(c,Time(2000),step_started_) && result.captured>confirmation_frame_) {
                confirmation_frame_=result.captured;
                if (!result.value) baseline_clear_=true;
                if (result.knowledge==Knowledge::present && result.value && baseline_clear_) {
                    if (pattern_) pattern_->stop("Result confirmed");
                    tasks_.release(scope_); complete_step(c,s); return true;
                }
            }
        }
        if (!pattern_) {
            pattern_=std::make_unique<Runner>(std::make_unique<PatternProgram>(*pattern,config_.speed),permissions_,tasks_,scope_,navigation_services_,depth_+1); pattern_->configure_conversion(hive_conversion_); pattern_->configure_statistics(recovery_counts_); pattern_->configure_reporting(activity_sink_); pattern_->start(c);
            pattern_->parent_menu_=[this] { return movement_menu(); };
        }
        pattern_->update(c,s); const auto result=pattern_->progress(); progress_.message=pattern->label;
        if (result.state==State::completed) {
            if (pattern->until || pattern->outcome) interrupt(Interruption::lost_observations,c,s,"Activity ended without a confirmed result");
            else complete_step(c,s);
        } else if (result.state==State::deferred) finish(State::deferred,result.message);
        else if (result.state==State::failed && result.interruption) interrupt(*result.interruption,c,s,result.message);
        else if (result.state==State::failed || result.state==State::stopped) finish(State::failed,result.message);
        return true;
    }
    throw std::logic_error("Unsupported activity");
}
}
