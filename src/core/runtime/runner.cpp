#include "runner.hpp"
#include "guards.hpp"
#include "game/detection/connection.hpp"
#include <algorithm>

namespace hive::automation {
Runner::Runner(std::unique_ptr<Program> program,Permissions permissions,TaskPort& tasks,ModuleScope scope,NavigationServices navigation,unsigned depth)
    : program_(std::move(program)),permissions_(std::move(permissions)),tasks_(tasks),scope_(scope),depth_(depth),navigation_services_(std::move(navigation)) {
    if (depth_>8) throw std::invalid_argument("Workflow nesting exceeds eight levels");
    if (bool(navigation_services_.routes)!=bool(navigation_services_.perception)) throw std::invalid_argument("Navigation requires routes and perception together");
    if (!program_) throw std::invalid_argument("Missing feature program");
    for (auto permission : permissions_) if (permission!=Permission::movement && permission!=Permission::tool && permission!=Permission::interact)
        throw std::invalid_argument("Unsupported feature permission");
    config_=program_->config();
    if (config_.duration<Time(1000) || config_.duration>std::chrono::hours(8)
        || !std::isfinite(config_.speed) || config_.speed<1 || config_.speed>200)
        throw std::invalid_argument("Invalid feature duration or movement speed");
    speed_=std::make_shared<game::MovementSpeed>(config_.speed);
    guard_=program_->guard();
    if (!guard_) guard_=observation_guard(config_);
}
Progress Runner::progress() const {
    auto result=progress_;
    if (result.state!=State::running) return result;
    const auto child=child_?std::optional(child_->progress()):std::nullopt;
    if (recovery_ || awaiting_window_) result.activity=WorkActivity::recovering;
    else if (conversion_) result.activity=WorkActivity::converting;
    else if (navigation_ || checkpoint_) result.activity=WorkActivity::travelling;
    else if (pattern_ && step_) result.activity=std::get<Pattern>(*step_).activity;
    else if (child) result.activity=child->activity;
    if (result.activity==WorkActivity::other) result.activity=program_->activity();
    if (result.activity==WorkActivity::other && step_ && std::holds_alternative<Walk>(*step_)) result.activity=WorkActivity::travelling;
    if (awaiting_window_) result.status={"Rejoining Bee Swarm",{}};
    else if (recovery_) result.status=recovery_->progress().status;
    else result.status=program_->activity_status();
    if (result.status.empty() && child) result.status=child->status;
    if (result.activity==WorkActivity::recovering && result.status.empty()) result.status={"Recovering",{}};
    // A local observation wait is still part of the current activity. Keep its
    // technical reason in message instead of alternating public status updates.
    if (waiting_for_menu() && result.status.empty()) result.status={"Preparing game",{},ActivityKind::diagnostic};
    result.activity_label=result.status.text();
    result.field=program_->field();
    if (result.field.empty() && child) result.field=child->field;
    if (!recovery_ && !awaiting_window_ && !navigation_ && !checkpoint_ && !conversion_) {
        result.shared_activity=program_->shared_activity();
        if (!result.shared_activity && child) result.shared_activity=child->shared_activity;
    }
    return result;
}
Runner::~Runner() { if (progress_.state==State::running || owned_ || !pending_.empty()) finish(State::stopped,"Stopped"); }
void Runner::check_scope(ModuleScope scope) const {
    if (scope!=scope_) throw std::logic_error("Nested operation belongs to a different workflow");
}
bool Runner::acquire(ModuleScope scope) {
    check_scope(scope);
    if (!owned_) owned_=tasks_.acquire(scope_);
    return owned_;
}
void Runner::release(ModuleScope scope) { check_scope(scope); }
std::string Runner::submit(ModuleScope scope,TaskClass kind,std::vector<Action> actions,Time lifetime) {
    check_scope(scope);
    if (!owned_) throw std::logic_error("Nested operation does not own input");
    for (const auto& action : actions) for (const auto key : action.held)
        if (key==Key::interact && !permissions_.contains(Permission::interact))
            throw std::runtime_error("Nested interaction permission was not granted");
    if (navigation_ && travel_pointer_) for (auto& action:actions)
        if (allows_concurrent_pointer(action))
            action.concurrent_pointer=travel_pointer_;
    const bool changes_ui=SidebarState::affects_ui(actions);
    auto id=tasks_.submit(scope_,kind,std::move(actions),lifetime);
    if (changes_ui) sidebar_->submitted(id);
    if (navigation_) navigation_input_=id;
    return id;
}
std::vector<TaskResult> Runner::results(ModuleScope scope) {
    check_scope(scope); auto results=tasks_.results(scope_);
    for (const auto& result:results) {
        sidebar_->completed(result);
        if (result.id==navigation_input_) navigation_input_.clear();
    }
    return results;
}
void Runner::cancel(ModuleScope scope) {
    check_scope(scope);
    if (!navigation_ || !navigation_input_.empty()) tasks_.cancel(scope_);
    navigation_input_.clear();
}
bool Runner::same_context(const ObservationContext& c) const {
    return c.generation==scope_.generation && c.generation==context_.generation && c.window.id==context_.window.id
        && c.window.process==context_.window.process && c.window.bounds==context_.window.bounds;
}
bool Runner::waiting_for_game_view() const noexcept {
    if (progress_.state==State::running && window_replaced_ && recovery_)
        return recovery_->waiting_for_game_view();
    return progress_.state==State::running && startup_pending_ && !progress_.steps && !step_
        && !recovery_ && !child_ && !pattern_ && !owned_ && pending_.empty();
}
void Runner::rebase_startup_window(const ObservationContext& c) {
    if (!waiting_for_game_view() || !c.window.id || !c.window.bounds.valid() || c.generation!=scope_.generation
        || c.window.process!=context_.window.process || c.now<last_)
        throw std::logic_error("Game window cannot change after startup work begins");
    if (recovery_) recovery_->rebase_startup_window(c);
    // Preserve the loading deadline across the client's resize/maximize events.
    context_.window=c.window; last_=c.now; speed_->start(c);
}
void Runner::configure_connection_recovery(ProgramFactory reconnect,ProgramFactory replacement) {
    if (progress_.state!=State::ready) throw std::logic_error("Configure connection recovery before starting");
    if (bool(reconnect)!=bool(replacement)) throw std::invalid_argument("Connection recovery requires both connection factories");
    connection_reconnect_=std::move(reconnect); connection_replacement_=std::move(replacement);
    startup_pending_=bool(connection_reconnect_) && permissions_.contains(Permission::interact);
}
void Runner::start(const ObservationContext& c,bool joining) {
    if (progress_.state!=State::ready || !c.window.id || !c.window.bounds.valid() || c.generation!=scope_.generation)
        throw std::logic_error("Feature needs a new program and valid session");
    startup_joining_=startup_joining_ || joining;
    if (!depth_) sidebar_->start(c.now);
    context_=c; speed_->start(c); last_=prerequisites_started_=c.now; clock_started_=!guard_; progress_.state=State::running; progress_.message="Running feature";
}
void Runner::clear_operations() {
    if (pattern_) { pattern_->stop(); pattern_.reset(); }
    if (child_) { child_->stop(); child_.reset(); }
    if (recovery_) { recovery_->stop(); recovery_.reset(); }
    replacement_={}; awaiting_window_=false; rejoin_requested_=false;
    navigation_.reset();
    if (travel_pointer_) {
        travel_pointer_->cancel(scope_);
        sidebar_->changed(travel_pointer_->menu_changed());
    }
    travel_inventory_.reset(); travel_pointer_.reset(); navigation_input_.clear();
    progress_.route_execution.reset();
    std::exception_ptr error;
    try { tasks_.cancel(scope_); results(scope_); tasks_.release(scope_); }
    catch (...) { error=std::current_exception(); }
    sidebar_->cancelled(last_);
    owned_=false; pending_.clear(); menu_pending_.clear(); menu_after_=menu_retry_at_=menu_retry_delay_={}; menu_waiting_=false; closing_menu_.reset(); step_.reset(); interaction_.reset(); conversion_.reset(); checkpoint_.reset(); sprinkler_.reset(); inventory_.reset(); dialog_.reset();
    progress_.estimated_heading.reset(); pending_heading_.reset();
    if (error) std::rethrow_exception(error);
}
void Runner::finish(State state,std::string message,std::optional<Interruption> interruption) {
    const auto current=progress();
    progress_.activity_label=current.activity_label; progress_.status=current.status;
    try { clear_operations(); }
    catch (const std::exception& e) { progress_.input_released=false; state=State::failed; interruption.reset(); message+="; "+std::string(e.what()); }
    catch (...) { progress_.input_released=false; state=State::failed; interruption.reset(); message="Could not release feature input"; }
    if (state!=State::completed) progress_.route_position.reset();
    try { program_->finished(state); }
    catch (const std::exception& e) { state=State::failed; interruption.reset(); message+="; "+std::string(e.what()); }
    catch (...) { state=State::failed; interruption.reset(); message="Could not finalize feature state"; }
    if (state!=State::completed) progress_.route_position.reset();
    else progress_.estimated_heading=current.estimated_heading;
    progress_.state=state; progress_.message=std::move(message);
    progress_.interruption=state==State::failed?interruption:std::nullopt;
}
void Runner::stop(std::string reason) { if (progress_.state==State::running || progress_.state==State::paused) finish(State::stopped,std::move(reason)); }
void Runner::fail(std::string reason) { if (progress_.state==State::running || progress_.state==State::paused) finish(State::failed,std::move(reason)); }
bool Runner::request_yield() {
    if (progress_.state!=State::running) return false;
    try { return program_->request_yield(); }
    catch (const std::exception& e) { fail(e.what()); }
    catch (...) { fail("Module could not yield"); }
    return false;
}
void Runner::pause(Time now,std::string reason) {
    if (progress_.state!=State::running) return;
    if (now<last_) { finish(State::failed,"Clock moved backwards"); return; }
    if (clock_started_) progress_.elapsed+=now-last_;
    last_=now;
    finish(State::paused,std::move(reason)+". Check the game state before restarting this feature.");
}
void Runner::resume(const ObservationContext&) {
    throw std::logic_error("Start a new feature run after checking the game state; partial operations cannot be resumed safely yet");
}
bool Runner::expects_respawn() const noexcept {
    return progress_.state==State::running && (program_->expects_respawn() || (child_ && child_->expects_respawn())
        || (pattern_ && pattern_->expects_respawn()) || (recovery_ && recovery_->expects_respawn()));
}
bool Runner::recover(Interruption interruption,const ObservationContext& c,const game::State& observations) {
    if (progress_.state!=State::running) return false;
    if (recovery_) return interruption==Interruption::death && recovery_->recover(interruption,c,observations);
    const View view{progress_.elapsed,progress_.steps,progress_.backpack_percent,c,&observations};
    const bool connection=interruption==Interruption::disconnected && connection_reconnect_
        && permissions_.contains(Permission::interact);
    if (connection && connection_attempts_>=3) return false;
    const bool resume=connection && startup_pending_;
    const auto factory=connection?connection_reconnect_:program_->recovery(interruption,view);
    if (!factory) {
        if (!child_ || !child_->recover(interruption,c,observations)) return false;
        progress_.route_position.reset(); progress_.route_execution.reset(); progress_.estimated_heading.reset();
        return true;
    }
    clear_operations(); progress_.route_position.reset(); progress_.interruption.reset(); speed_->start(c);
    window_replaced_=false;
    if (connection) {
        if (!resume && recovery_counts_ && !recovery_counts_->disconnected) {
            ++recovery_counts_->disconnects; recovery_counts_->disconnected=true;
        }
        replacement_=connection_replacement_; startup_pending_=false;
        connection_recovering_=true; resume_connected_=resume; ++connection_attempts_;
    }
    recovery_=std::make_unique<Runner>(factory(),permissions_,static_cast<TaskPort&>(*this),scope_,navigation_services_,depth_+1); recovery_->configure_conversion(hive_conversion_); recovery_->configure_statistics(recovery_counts_); recovery_->configure_reporting(activity_sink_);
    recovery_->sidebar_=sidebar_;
    recovery_->start(c); progress_.message=connection?"Recovering game connection":"Recovering at hive";
    return true;
}
bool Runner::expects_reconnect() const noexcept {
    return progress_.state==State::running && (program_->expects_reconnect() || (child_ && child_->expects_reconnect())
        || (recovery_ && recovery_->expects_reconnect()));
}
bool Runner::can_replace_window() const noexcept {
    return progress_.state==State::running && !awaiting_window_ && !window_replaced_ && replacement_
        && recovery_ && recovery_->expects_reconnect();
}
bool Runner::server_change_ready() const noexcept {
    return progress_.state==State::running && program_->server_change_ready()
        && !recovery_ && !child_ && !pattern_ && pending_.empty()
        && (!step_ || std::holds_alternative<Wait>(*step_));
}
bool Runner::prepare_client_rejoin(Time now,bool requested) {
    if (!allow_rejoin_ || progress_.state!=State::running) return false;
    if (requested && connection_replacement_ && permissions_.contains(Permission::interact)
        && (waiting_for_game_view() || server_change_ready())) {
        // Planned hops are not connection failures and do not consume their retry budget.
        resume_connected_=startup_pending_; startup_pending_=false;
        clear_operations(); replacement_=program_->server_arrival();
        if (!replacement_) replacement_=connection_replacement_;
        connection_recovering_=true; window_replaced_=false;
        awaiting_window_=rejoin_requested_=true;
        progress_.route_position.reset(); progress_.route_execution.reset(); progress_.estimated_heading.reset();
        progress_.backpack_percent.reset();
        return wait_window(now,"Joining the requested server");
    }
    if (needs_client_rejoin()) return true;
    if (awaiting_window_ && replacement_) { rejoin_requested_=true; return wait_window(now); }
    if (!can_replace_window()) {
        if (recovery_ || !recover(Interruption::disconnected,{context_.window,scope_.generation,now},{})) return false;
    }
    if (!suspend_window(now)) return false;
    rejoin_requested_=true;
    return true;
}
bool Runner::suspend_window(Time now) {
    if (!can_replace_window()) return false;
    auto replacement=replacement_;
    clear_operations(); replacement_=std::move(replacement); awaiting_window_=true;
    progress_.route_position.reset(); progress_.backpack_percent.reset();
    progress_.message="Waiting for Roblox's replacement window";
    return wait_window(now);
}
bool Runner::wait_window(Time now,const std::string& status) {
    if (!awaiting_window_ || progress_.state!=State::running) return false;
    if (now<last_) { finish(State::failed,"Clock moved backwards during reconnect"); return false; }
    // Client launch has its own bounded deadline; it is not workflow execution.
    last_=now;
    if (!status.empty()) progress_.message=status;
    if (progress_.elapsed>=config_.duration) { finish(State::failed,"Workflow deadline reached during reconnect"); return false; }
    return true;
}
void Runner::replace_window(const ObservationContext& c,ModuleScope scope) {
    if (!awaiting_window_ || progress_.state!=State::running || !replacement_ || !c.window.id || !c.window.bounds.valid()
        || !scope.id || scope.generation<=scope_.generation || c.generation!=scope.generation || c.now<last_)
        throw std::logic_error("Replacement window requires a suspended recovery and fresh scope");
    scope_=scope; context_=c; last_=prerequisites_started_=c.now; speed_->start(c);
    if (progress_.elapsed>=config_.duration) { finish(State::failed,"Workflow deadline reached during reconnect"); return; }
    guard_=program_->guard(); if (!guard_) guard_=observation_guard(config_);
    clock_started_=true;
    recovery_=std::make_unique<Runner>(replacement_(),permissions_,static_cast<TaskPort&>(*this),scope_,navigation_services_,depth_+1); recovery_->configure_conversion(hive_conversion_); recovery_->configure_statistics(recovery_counts_); recovery_->configure_reporting(activity_sink_);
    recovery_->sidebar_=sidebar_;
    recovery_->startup_pending_=recovery_->startup_joining_=true;
    recovery_->start(c); awaiting_window_=false; rejoin_requested_=false; window_replaced_=true;
    progress_.message="Confirming the replacement game window";
}
void Runner::interrupt(Interruption interruption,const ObservationContext& c,const game::State& observations,std::string message) {
    try {
        const auto failed=progress();
        const bool recovered=interruption==Interruption::client_rejoin?prepare_client_rejoin(c.now):recover(interruption,c,observations);
        if (recovered && activity_sink_) {
            if (interruption==Interruption::death) activity_sink_({{"Respawning","Returning to hive"}});
            else if (interruption==Interruption::disconnected || interruption==Interruption::client_rejoin)
                activity_sink_({{"Rejoining Bee Swarm",message}});
            else {
                const auto action=failed.status.empty()?failed.activity_label:failed.status.text();
                activity_sink_({{"Recovering",action.empty()?message:action+" · "+message,ActivityKind::recovery}});
            }
        }
        if (!recovered) finish(State::failed,std::move(message),interruption);
    }
    catch (const ClientRejoinRequired& e) {
        if (interruption==Interruption::client_rejoin) finish(State::failed,e.what(),interruption);
        else interrupt(Interruption::client_rejoin,c,observations,e.what());
    }
    catch (const std::exception& e) { finish(State::failed,e.what()); }
    catch (...) { finish(State::failed,"Feature recovery failed unexpectedly"); }
}
void Runner::observe_movement(const ObservationContext& c,const game::State& observations) {
    if (progress_.state!=State::running || !same_context(c) || c.now<last_) return;
    try {
        if (step_ && std::holds_alternative<Walk>(*step_)) speed_->observe(c,observations.movement_speed);
        if (child_) child_->observe_movement(c,observations);
        if (pattern_) pattern_->observe_movement(c,observations);
        if (recovery_) recovery_->observe_movement(c,observations);
    } catch (const std::exception& e) { fail(e.what()); }
    catch (...) { fail("Movement observation failed"); }
}
void Runner::update(const ObservationContext& c,const game::State& observations) {
    if (progress_.state!=State::running) return;
    if (awaiting_window_) { wait_window(c.now); return; }
    if (!same_context(c) || c.now<last_) { finish(State::failed,"Session or window changed"); return; }
    // A delayed UI observation pauses this step, not the entire game session.
    // Preserve parent deadlines too when a nested camera step is waiting.
    if (waiting_for_menu()) step_started_+=c.now-last_;
    else if (clock_started_ && !waiting_for_game_view()) progress_.elapsed+=c.now-last_;
    last_=c.now;
    progress_.backpack_percent=backpack_reading(c,observations,context_.now);
    try {
        if (!expects_reconnect() && observations.connection.has_value(c,Time(2000),context_.now)
            && observations.connection.value==game::Connection::disconnected) {
            interrupt(Interruption::disconnected,c,observations,"Game disconnected"); return;
        }
        if (expects_respawn() || expects_reconnect()) clock_started_=true;
        Time death{Time(-1)};
        if (observations.last_death.has_value(c,Time::max(),context_.now) && observations.last_death.value)
            death=observations.last_death.captured;
        if (observations.alive.has_value(c,Time(2000),context_.now) && !observations.alive.value)
            death=std::max(death,observations.alive.captured);
        if (death>death_seen_) {
            death_seen_=death;
            if (!expects_respawn()) {
                interrupt(Interruption::death,c,observations,"Player respawning");
                return;
            }
        }
        if (recovery_) {
            if (progress_.elapsed>=config_.duration) { finish(State::failed,"Workflow deadline reached during recovery"); return; }
            recovery_->update(c,observations);
            const auto result=recovery_->progress(); progress_.message=result.message;
            if (result.state==State::completed) {
                progress_.route_position=result.route_position; progress_.estimated_heading=result.estimated_heading;
                recovery_.reset();
                replacement_={};
                if (connection_recovering_) {
                    connection_recovering_=false;
                    connection_attempts_=0;
                    prerequisites_started_=c.now; clock_started_=!guard_;
                    if (recovery_counts_ && recovery_counts_->disconnected) {
                        ++recovery_counts_->rejoins; recovery_counts_->disconnected=false;
                    }
                    if (!resume_connected_ && !program_->resume_after_reconnect({progress_.elapsed,progress_.steps,progress_.backpack_percent,c,&observations})) {
                        finish(State::failed,"Game reconnected; interrupted work requires a fresh run"); return;
                    }
                }
            } else if (result.state==State::failed && (result.interruption==Interruption::disconnected || result.interruption==Interruption::client_rejoin)
                && allow_rejoin_ && replacement_ && (!window_replaced_ || connection_attempts_<3)) {
                // A failed replacement still belongs to this recovery. Preserve
                // its arrival program and use the remaining consecutive attempts.
                if (window_replaced_) ++connection_attempts_;
                auto replacement=replacement_;
                clear_operations(); replacement_=std::move(replacement); awaiting_window_=rejoin_requested_=true;
                window_replaced_=false;
                progress_.route_position.reset(); progress_.backpack_percent.reset();
                progress_.message="Roblox needs to rejoin";
                if (activity_sink_) activity_sink_({{"Retrying Bee Swarm join",result.message}});
            } else if (result.state==State::failed && (result.interruption==Interruption::disconnected || result.interruption==Interruption::client_rejoin) && !connection_recovering_) {
                recovery_.reset();
                interrupt(*result.interruption,c,observations,result.message);
            } else if (result.state!=State::running) finish(State::failed,result.message,result.interruption);
            return;
        }
        if (startup_pending_) {
            const bool hud=observations.game_hud.has_value(c,Time(2000),context_.now) && observations.game_hud.value;
            const bool connected=observations.connection.has_value(c,Time(2000),context_.now)
                && observations.connection.value==game::Connection::connected;
            // A reconnect workflow must be allowed to handle a disconnect dialog
            // encountered during loading, before the game HUD can return.
            const bool reconnect_dialog=expects_reconnect()
                && observations.connection.has_value(c,Time(2000),context_.now)
                && observations.connection.value==game::Connection::disconnected;
            if (!hud && !connected && !reconnect_dialog) {
                // Roblox can show Home while an already requested join opens.
                // Let that launch use its loading budget instead of relaunching
                // on each Home frame and exhausting all recovery attempts.
                if (!startup_joining_ && !expects_reconnect() && observations.client_home.has_value(c,Time(2000),context_.now)
                    && observations.client_home.value) {
                    interrupt(Interruption::client_rejoin,c,observations,"Roblox is on its home screen"); return;
                }
                if (c.now-context_.now>=game_loading_timeout) {
                    const bool observed=observations.game_hud.usable(c,Time(2000),context_.now);
                    interrupt(Interruption::client_rejoin,c,observations,observed?
                        "Bee Swarm game view was not recognized within 3 minutes":"No fresh game view was available within 3 minutes");
                }
                else progress_.message="Waiting for Bee Swarm to load";
                return;
            }
            startup_pending_=false; prerequisites_started_=c.now;
        }
        if (guard_ && !expects_respawn()) {
            const auto ready_since=std::max(context_.now,prerequisites_started_);
            const auto decision=guard_->evaluate({progress_.elapsed,progress_.steps,progress_.backpack_percent,c,&observations,progress_.estimated_heading},ready_since);
            switch (decision.state) {
            case GuardState::waiting:
                if (clock_started_) interrupt(Interruption::lost_observations,c,observations,decision.message.empty()?"Required observations were lost during execution":decision.message);
                else if (c.now-ready_since>=Time(10000)) finish(State::failed,"Feature prerequisites were not ready before the startup deadline");
                else progress_.message=decision.message.empty()?"Waiting for feature prerequisites":decision.message;
                return;
            case GuardState::failed:
                if (decision.interruption) interrupt(*decision.interruption,c,observations,decision.message);
                else finish(State::failed,decision.message.empty()?"Feature prerequisites failed":decision.message);
                return;
            case GuardState::completed:
                finish(config_.require_done?State::failed:State::completed,config_.require_done?"Stop condition reached before workflow completion":decision.message); return;
            case GuardState::ready: clock_started_=true; break;
            default: throw std::invalid_argument("Invalid run guard decision");
            }
        }
        if (progress_.elapsed>=config_.duration) {
            const bool unfinished=config_.require_done || (step_ && !std::holds_alternative<Walk>(*step_) && !std::holds_alternative<Hold>(*step_));
            finish(unfinished?State::failed:State::completed,unfinished?"Workflow deadline reached before confirmation":"Run time reached",expects_reconnect()?std::optional(Interruption::disconnected):std::nullopt); return;
        }
        program_->observe({progress_.elapsed,progress_.steps,progress_.backpack_percent,c,&observations,
            progress_.estimated_heading,progress_.route_position,progress_.route_execution});
        if (step_ && std::holds_alternative<Walk>(*step_)) speed_->observe(c,observations.movement_speed);
        const bool movement_finished=step_ && menu_pending_.empty() && std::visit([](const auto& step) {
            using T=std::decay_t<decltype(step)>;
            if constexpr (std::is_same_v<T,Walk> || std::is_same_v<T,FollowPath> || std::is_same_v<T,Navigate>)
                return step.until && step.until();
            return false;
        },*step_);
        if (movement_finished) {
            // Runtime cancellation releases input and integrates its final movement
            // sample before notifying the program that this leg has ended.
            if (!pending_.empty() || checkpoint_ || navigation_) {
                tasks_.cancel(scope_);
                for (const auto& result:results(scope_)) if (result.outcome!=TaskOutcome::cancelled && result.outcome!=TaskOutcome::executed) {
                    finish(State::failed,"Could not stop observed movement: "+result.reason); return;
                }
                pending_.clear();
            }
            if (checkpoint_ || navigation_) {
                progress_.route_position.reset(); progress_.route_execution.reset();
            }
            complete_step(c,observations);
            return;
        }
        if (!pending_.empty()) for (const auto& result : results(scope_)) if (result.id==pending_) {
            pending_.clear();
            if (result.outcome!=TaskOutcome::executed) {
                if (result.outcome==TaskOutcome::expired)
                    interrupt(Interruption::lost_route,c,observations,"Input timed out before the action finished");
                else finish(State::failed,"Input interrupted"+(result.reason.empty()?std::string{}:": "+result.reason));
                return;
            }
            const bool walking=step_ && std::holds_alternative<Walk>(*step_);
            if (!walking) hold_left_-=result.execution_time;
            if (walking || hold_left_<=Time::zero()) {
                progress_.estimated_heading=pending_heading_?pending_heading_:progress_.estimated_heading;
                pending_heading_.reset();
                if (!progress_.estimated_heading && step_ && (std::holds_alternative<UseControl>(*step_) || std::holds_alternative<MoveCamera>(*step_))) tasks_.release(scope_);
                complete_step(c,observations);
            }
        }
        if (!pending_.empty()) return;
        if (!step_) {
            if (progress_.route_position && !progress_.route_position->usable(c)) progress_.route_position.reset();
            step_=program_->next({progress_.elapsed,progress_.steps,progress_.backpack_percent,c,&observations,progress_.estimated_heading,progress_.route_position});
            step_started_=c.now; confirmation_frame_=Time(-1); baseline_clear_=false;
            const auto* shortcut=std::get_if<TapMenuKey>(&*step_);
            // Showing chat and leaving its empty entry do not move the player or camera.
            const bool chat=shortcut && (shortcut->key==Key::chat || shortcut->key==Key::chat_submit);
            if (!std::holds_alternative<Done>(*step_) && !std::holds_alternative<Wait>(*step_) && !std::holds_alternative<EndCycle>(*step_)
                && !std::holds_alternative<AwaitText>(*step_) && !std::holds_alternative<Navigate>(*step_)
                && !std::holds_alternative<RunProgram>(*step_) && !chat) progress_.route_position.reset();
            if (!std::holds_alternative<MoveCamera>(*step_) && !std::holds_alternative<SetHeading>(*step_)
                && !std::holds_alternative<Wait>(*step_) && !std::holds_alternative<Walk>(*step_) && !std::holds_alternative<EndCycle>(*step_)
                && !std::holds_alternative<RunProgram>(*step_) && !std::holds_alternative<Done>(*step_) && !chat) progress_.estimated_heading.reset();
            if (auto* done=std::get_if<Done>(&*step_)) { finish(State::completed,done->reason); return; }
            if (auto* deferred=std::get_if<Defer>(&*step_)) { finish(State::deferred,deferred->reason); return; }
            if (auto* walk=std::get_if<Walk>(&*step_)) { speed_->observe(c,observations.movement_speed); }
            else if (const auto* hold=std::get_if<Hold>(&*step_)) {
                hold_left_=hold->duration;
                if (hold->duration<Time(1) || hold->duration>Time(10000) || hold->keys.size()>3)
                    throw std::invalid_argument("Invalid key action");
            }
        }
        if (prepare_menu(c,observations)) return;
        if (activity(c,observations)) return;
        if (!owned_) { if (!tasks_.acquire(scope_)) { progress_.message="Waiting for game control"; return; } owned_=true; }
        const auto remaining=config_.duration-progress_.elapsed;
        Action action;
        if (auto* walk=std::get_if<Walk>(&*step_)) {
            action=game::paced_walk(walk->leg,speed_,c,walk->tool,walk->distance);
            progress_.message="Following pattern";
        } else { const auto& hold=std::get<Hold>(*step_); action={"Pattern action",hold.keys,std::min(hold_left_,remaining),hold.pointer}; }
        for (auto key : action.held) {
            if (key<Key::forward || (key>Key::primary && key!=Key::shift_lock && key!=Key::interact && !hotbar_key(key))) throw std::invalid_argument("Unsupported program input");
            const auto permission=(key==Key::interact || hotbar_key(key))?Permission::interact:key==Key::primary?Permission::tool:Permission::movement;
            if (!permissions_.contains(permission)) throw std::runtime_error("Feature requested undeclared input permission");
        }
        const auto lifetime=action.pacing?std::min(Time(600000),config_.duration-progress_.elapsed+Time(1000)):action.duration+Time(1000);
        std::vector<Action> actions;
        if (action.pacing) actions.push_back(std::move(action));
        else {
            const bool continuous=!action.pointer && std::all_of(action.held.begin(),action.held.end(),[](Key key) {
                return (key>=Key::forward && key<=Key::jump) || key==Key::primary;
            });
            auto left=action.duration;
            while (left>Time{}) {
                auto part=action; part.duration=std::min(left,Time(250)); left-=part.duration;
                part.continue_movement=continuous && left>Time{}; actions.push_back(std::move(part));
            }
        }
        pending_=submit(scope_,TaskClass::activity,std::move(actions),lifetime);
    } catch (const ClientRejoinRequired& e) { interrupt(Interruption::client_rejoin,c,observations,e.what()); }
    catch (const ObservationUnavailable& e) { interrupt(Interruption::lost_observations,c,observations,e.what()); }
    catch (const std::exception& e) { finish(State::failed,e.what()); }
    catch (...) { finish(State::failed,"Feature failed unexpectedly"); }
}
}
