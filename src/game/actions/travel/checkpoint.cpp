#include "checkpoint.hpp"
#include <algorithm>

namespace hive::workflows {
void validate(const CheckpointPlan& p) {
    if (p.observation_scope.size()>80 || std::any_of(p.observation_scope.begin(),p.observation_scope.end(),[](unsigned char c) { return c<32 || c==127; }))
        throw std::invalid_argument("Invalid route observation scope");
    if (p.minimum_duration<Time{} || p.minimum_duration>=p.timeout
        || (p.minimum_duration>Time{} && p.completion!=CheckpointCompletion::movement))
        throw std::invalid_argument("Invalid minimum movement duration");
    if (!p.sequence.empty()) {
        if (!p.continuous || p.completion!=CheckpointCompletion::movement || !p.approach.empty() || p.sequence.size()>512)
            throw std::invalid_argument("Distance sequences require continuous route movement");
        auto plain=p; plain.sequence.clear(); double duration=0;
        for (const auto& step : p.sequence) {
            if (const auto* leg=std::get_if<game::WalkLeg>(&step)) {
                plain.approach.push_back(game::movement_slice(*leg,leg->tiles,p.speed));
                duration+=std::ceil(leg->tiles*4000/p.speed);
            } else {
                const auto& action=std::get<Action>(step);
                plain.approach.push_back(action); duration+=action.duration.count();
            }
        }
        validate(plain);
        if (duration>=p.timeout.count()) throw std::invalid_argument("Movement sequence exceeds checkpoint timeout");
        return;
    }
    const bool motion=p.completion==CheckpointCompletion::movement;
    const bool launch=p.completion==CheckpointCompletion::launch;
    if (!p.launch_movement.empty()) {
        if (!launch || p.launch_movement.size()>2) throw std::invalid_argument("Launch movement requires a transport checkpoint");
        std::set<Key> keys;
        for (auto key:p.launch_movement)
            if (key<Key::forward || key>Key::right || !keys.insert(key).second) throw std::invalid_argument("Invalid launch movement");
        if ((keys.contains(Key::forward) && keys.contains(Key::backward)) || (keys.contains(Key::left) && keys.contains(Key::right)))
            throw std::invalid_argument("Opposing launch movement");
    }
    if ((!motion && p.detector.empty()) || p.detector.size()>120 || p.timeout<Time(100) || p.timeout>Time(120000)
        || p.observation_age<Time(1) || p.observation_age>Time(2000) || p.attempts>16
        || p.camera_settle<Time(1) || p.camera_settle>Time(2000))
        throw std::invalid_argument("Invalid checkpoint bounds");
    if (p.completion!=CheckpointCompletion::observation && !motion && !launch) throw std::invalid_argument("Invalid checkpoint completion policy");
    if (launch && (!p.continuous || p.detector.empty() || !p.nudge.empty() || p.approach.size()>127
        || (p.path.empty() && (p.attempts || !p.nudge_path.empty()))))
        throw std::invalid_argument("Launch checkpoints need one observed activation and bounded movement");
    if (p.continuous && ((!launch && !p.path.empty()) || p.approach.empty() || p.approach.size()>512))
        throw std::invalid_argument("Continuous checkpoints require a bounded movement sequence");
    if (motion && (!p.detector.empty() || p.attempts || !p.nudge.empty() || !p.nudge_path.empty()
        || (p.approach.empty() && p.path.empty()))) throw std::invalid_argument("Route motion cannot claim detection or retry");
    if ((!launch && !p.path.empty() && !p.approach.empty()) || (!p.nudge_path.empty() && !p.nudge.empty())
        || !std::isfinite(p.speed) || p.speed<1 || p.speed>200)
        throw std::invalid_argument("Invalid distance checkpoint path");
    if ((!launch && !p.launch_prepare.empty()) || p.launch_prepare.size()>32)
        throw std::invalid_argument("Transport aiming requires a launch checkpoint");
    Time aiming{};
    for (const auto& action:p.launch_prepare) {
        if (action.held.size()>1 || (!action.held.empty() && !camera_key(action.held.front()))
            || action.tap || action.pointer || action.pointer_to || action.scroll || action.pacing || action.continue_movement
            || action.tap_count!=1 || action.duration<Time(20) || action.duration>Time(200))
            throw std::invalid_argument("Transport aiming only accepts brief camera presses and releases");
        aiming+=action.duration;
    }
    for (const auto* path : {&p.path,&p.nudge_path}) {
        if (path->size()>64) throw std::invalid_argument("Too many checkpoint walk legs");
        double tiles=0;
        for (const auto& leg : *path) { game::movement_slice(leg,leg.tiles,p.speed); tiles+=leg.tiles; }
        if (tiles*4000/p.speed>=p.timeout.count()) throw std::invalid_argument("Distance movement exceeds checkpoint timeout");
    }
    for (const auto* actions : {&p.approach,&p.nudge}) {
        if (actions->size()>512) throw std::invalid_argument("Too many checkpoint actions");
        Time duration{};
        for (const auto& a : *actions) {
            const bool flight_turn=a.tap && p.continuous && (motion || launch)
                && (*a.tap==Key::rotate_left || *a.tap==Key::rotate_right)
                && std::all_of(a.held.begin(),a.held.end(),[](Key key) { return key>=Key::forward && key<=Key::right; });
            if (a.duration<Time(1) || a.duration>Time(250) || a.pointer || a.pointer_to || a.scroll || a.held.size()>3
                || a.continue_movement || a.pacing || a.tap_count<1 || a.tap_count>8 || (!a.tap && a.tap_count!=1)
                || (a.tap && *a.tap!=Key::jump && !flight_turn))
                throw std::invalid_argument("Checkpoint actions require bounded movement");
            std::set<Key> keys;
            for (auto key : a.held) {
                if (key!=Key::forward && key!=Key::backward && key!=Key::left && key!=Key::right
                    && key!=Key::jump && key!=Key::shift_lock && !camera_key(key))
                    throw std::invalid_argument("Checkpoint cannot perform interactions");
                if (key==Key::shift_lock && (!motion || p.continuous || a.held.size()!=1 || a.duration>Time(100)))
                    throw std::invalid_argument("Route shift lock requires a single brief press");
                const bool moving_turn=p.continuous && motion && (key==Key::rotate_left || key==Key::rotate_right)
                    && a.duration<=Time(100) && std::any_of(a.held.begin(),a.held.end(),[](Key k) { return k>=Key::forward && k<=Key::right; });
                if (camera_key(key) && a.held.size()!=1 && !moving_turn) throw std::invalid_argument("Camera checkpoints need one key at a time");
                if (p.continuous && camera_key(key) && !moving_turn) throw std::invalid_argument("Continuous checkpoints cannot turn the camera");
                if (!keys.insert(key).second) throw std::invalid_argument("Duplicate movement key");
            }
            if ((keys.contains(Key::forward) && keys.contains(Key::backward))
                || (keys.contains(Key::left) && keys.contains(Key::right))
                || (keys.contains(Key::rotate_left) && keys.contains(Key::rotate_right)))
                throw std::invalid_argument("Opposing checkpoint keys");
            duration+=a.duration;
        }
        if (duration+aiming+(launch?Time(100):Time{})>=p.timeout) throw std::invalid_argument("Movement exceeds checkpoint timeout");
    }
}
Checkpoint::Checkpoint(CheckpointPlan plan):plan_(std::move(plan)),speed_(std::make_shared<game::MovementSpeed>(plan_.speed)) {
    validate(plan_);
    if (plan_.completion==CheckpointCompletion::movement && !plan_.path.empty()) {
        for (const auto& leg:plan_.path) plan_.sequence.push_back(leg);
        plan_.path.clear(); plan_.continuous=true;
    }
}
void Checkpoint::start(const ObservationContext& c,ModuleScope scope) {
    if (stage_!=CheckpointStage::idle || !scope.id || scope.generation!=c.generation || !c.generation
        || !c.window.id || !c.window.bounds.valid() || c.now<Time{})
        throw std::logic_error("Checkpoint needs a new valid session and scope");
    started_=c; speed_->start(c); scope_=scope; last_=minimum_=c.now; stage_=CheckpointStage::waiting;
    for (const auto& step : plan_.sequence) {
        if (const auto* leg=std::get_if<game::WalkLeg>(&step)) {
            auto action=game::paced_walk(*leg,speed_,c);
            plan_.approach.push_back(std::move(action));
        } else plan_.approach.push_back(std::get<Action>(step));
    }
}
Time Checkpoint::cancel_tasks(TaskPort& tasks,Time stopped) {
    tasks.cancel(scope_);
    for (const auto& result : tasks.results(scope_)) {
        if (result.outcome==TaskOutcome::faulted) throw std::runtime_error("Checkpoint input could not be released");
        if (result.id==pending_ && distance_ && plan_.completion!=CheckpointCompletion::launch && result.execution_time>Time{}) {
            auto context=started_; context.now=std::max(last_,result.finished); speed_->observe(context,{});
            speed_->advance(*distance_,result.finished,result.execution_time);
        }
        stopped=std::max(stopped,result.finished);
    }
    return stopped;
}
void Checkpoint::finish(CheckpointStage stage,std::string reason,TaskPort& tasks) {
    stage_=stage; reason_=std::move(reason);
    if (owned_) {
        try { cancel_tasks(tasks); }
        catch (const std::exception& e) { stage_=CheckpointStage::failed; failure_=CheckpointFailure::input; reason_+="; "+std::string(e.what()); }
        try { tasks.release(scope_); }
        catch (const std::exception& e) { stage_=CheckpointStage::failed; failure_=CheckpointFailure::input; reason_+="; "+std::string(e.what()); }
    }
    owned_=false; pending_.clear();
}
void Checkpoint::stop(TaskPort& tasks) {
    if (stage_==CheckpointStage::waiting || stage_==CheckpointStage::moving || stage_==CheckpointStage::confirming)
        finish(CheckpointStage::stopped,"Checkpoint stopped",tasks);
}
void Checkpoint::confirm(Time stopped) {
    minimum_=stopped+std::max(Time(1),plan_.minimum_duration-executed_);
    awaiting_view_=false; distance_.reset(); stage_=CheckpointStage::confirming;
}
void Checkpoint::update(const ObservationContext& c,const Observation<bool>& detected,TaskPort& tasks,const Observation<double>& speed) {
    if (stage_!=CheckpointStage::waiting && stage_!=CheckpointStage::moving && stage_!=CheckpointStage::confirming) return;
    auto fail=[&](CheckpointFailure failure,std::string reason) { failure_=failure; finish(CheckpointStage::failed,std::move(reason),tasks); };
    if (c.now<last_ || c.generation!=started_.generation || c.window.id!=started_.window.id
        || c.window.process!=started_.window.process || c.window.bounds!=started_.window.bounds) {
        fail(CheckpointFailure::context,"Checkpoint context changed"); return;
    }
    last_=c.now;
    if (c.now-started_.now>=plan_.timeout) { fail(launched_?CheckpointFailure::interrupted:CheckpointFailure::timeout,"Checkpoint was not confirmed before its deadline"); return; }
    try {
        if (!plan_.path.empty() || !plan_.nudge_path.empty() || !plan_.sequence.empty()) speed_->observe(c,speed);
        if (!owned_) {
            if (!tasks.acquire(scope_)) return;
            owned_=true; minimum_=c.now+(plan_.completion==CheckpointCompletion::launch && plan_.path.empty()?Time(1):Time{});
        }
        for (const auto& result : tasks.results(scope_)) if (result.id==pending_) {
            pending_.clear();
            if (result.outcome!=TaskOutcome::executed) { fail(CheckpointFailure::interrupted,"Checkpoint movement was interrupted"); return; }
            executed_+=result.execution_time;
            const auto& path=nudging_?plan_.nudge_path:plan_.path;
            if (!path.empty()) {
                if (plan_.completion==CheckpointCompletion::launch) {
                    distance_.reset();
                    if (++index_==path.size()) confirm(result.finished);
                } else {
                    speed_->advance(*distance_,result.finished,result.execution_time);
                    if (distance_->complete()) { distance_.reset(); if (++index_==path.size()) confirm(result.finished); }
                }
                continue;
            }
            const auto& actions=nudging_?plan_.nudge:plan_.approach;
            if (plan_.continuous) { index_=actions.size(); confirm(result.finished); continue; }
            const auto& previous=actions.at(index_++);
            const bool camera=previous.held.size()==1 && camera_key(previous.held.front());
            if (index_==actions.size()) confirm(result.finished+(camera?plan_.camera_settle:Time::zero()));
            else if (camera) { awaiting_view_=true; minimum_=result.finished+plan_.camera_settle; }
        }
        const bool motion=plan_.completion==CheckpointCompletion::movement;
        const bool launch=plan_.completion==CheckpointCompletion::launch;
        if ((motion || launched_) && stage_==CheckpointStage::confirming) {
            if (c.now>=minimum_) finish(CheckpointStage::completed,"Route movement completed",tasks);
            return;
        }
        const bool observed=!motion && detected.usable(c,plan_.observation_age,minimum_);
        const bool fresh=observed && detected.captured>seen_;
        if (!motion && !launched_ && !plan_.path.empty() && stage_==CheckpointStage::moving && !observed && c.now-seen_>plan_.observation_age) {
            if (!observation_paused_) {
                minimum_=cancel_tasks(tasks,c.now)+Time(1); pending_.clear(); observation_paused_=true;
                reason_="Waiting for fresh route observations; movement released";
            }
            return;
        }
        const bool present=fresh && detected.knowledge==Knowledge::present && detected.value;
        if (fresh) seen_=detected.captured;
        if (observation_paused_) {
            if (!fresh) return;
            observation_paused_=false;
            reason_.clear();
            // This frame was taken after input was released. A positive result
            // already confirms the stationary position; otherwise resume only
            // the untravelled distance, without restarting or backtracking.
            if (present) stage_=CheckpointStage::confirming;
        }
        if (launch && !launched_ && present && stage_!=CheckpointStage::confirming) {
            const auto stopped=pending_.empty()?c.now:cancel_tasks(tasks,c.now);
            pending_.clear();
            // A prompt captured during walking may already be behind us.
            // Release input and confirm from a new, stationary observation.
            confirm(stopped); return;
        }
        if (stage_==CheckpointStage::waiting) {
            if (launch && !present && plan_.path.empty()) return;
            // Observe before starting a search so an already-visible target
            // does not get left behind, including transport approaches.
            if (!motion && !fresh) return;
            if (present && !launch) { finish(CheckpointStage::completed,"Checkpoint confirmed",tasks); return; }
            if (!motion && !launch && plan_.approach.empty() && plan_.path.empty()) {
                if (!plan_.nudge.empty() || !plan_.nudge_path.empty()) confirm(c.now);
                return;
            }
            stage_=CheckpointStage::moving;
        } else if (stage_==CheckpointStage::moving && present && !launch) {
            const auto& actions=nudging_?plan_.nudge:plan_.approach;
            const bool camera=!actions.empty() && actions.at(std::min(index_,actions.size()-1)).held.size()==1
                && camera_key(actions.at(std::min(index_,actions.size()-1)).held.front());
            if (!camera) { finish(CheckpointStage::completed,"Checkpoint confirmed",tasks); return; }
            cancel_tasks(tasks); pending_.clear(); confirm(c.now+plan_.camera_settle); return;
        } else if (stage_==CheckpointStage::confirming) {
            if (!fresh) return;
            if (present) {
                if (!launch) { finish(CheckpointStage::completed,"Checkpoint confirmed",tasks); return; }
                plan_.path.clear(); plan_.nudge_path.clear();
                nudging_=false; index_=0; stage_=CheckpointStage::moving;
            } else {
                if (attempts_>=plan_.attempts || (plan_.nudge.empty() && plan_.nudge_path.empty())) { fail(CheckpointFailure::missing,"Checkpoint missing after movement"); return; }
                ++attempts_; nudging_=true; index_=0; distance_.reset(); stage_=CheckpointStage::moving;
            }
        }
        if (stage_==CheckpointStage::moving && pending_.empty()) {
            if (awaiting_view_) {
                if (motion?c.now<minimum_:!fresh) return;
                awaiting_view_=false;
            }
            const auto& actions=nudging_?plan_.nudge:plan_.approach;
            const auto& path=nudging_?plan_.nudge_path:plan_.path;
            if (!path.empty()) {
                const auto& leg=path.at(index_);
                // Keep walking while observations arrive independently. Paced walking
                // owns the distance bound; a seen prompt still cancels it immediately.
                if (launch) {
                    if (!distance_) distance_=std::make_shared<game::DistanceIntegrator>(leg.tiles);
                    if (distance_->complete()) { confirm(c.now); return; }
                    auto remaining=leg; remaining.tiles=distance_->remaining_tiles();
                    pending_=tasks.submit(scope_,TaskClass::travel,
                        {game::paced_walk(remaining,speed_,c,false,distance_)},plan_.timeout-(c.now-started_.now));
                }
                else {
                    if (!distance_) distance_=std::make_shared<game::DistanceIntegrator>(leg.tiles);
                    pending_=tasks.submit(scope_,TaskClass::travel,
                        {game::movement_slice(leg,distance_->remaining_tiles(),speed_->current())},plan_.timeout-(c.now-started_.now));
                }
            } else if (plan_.continuous) {
                auto sequence=actions;
                for (std::size_t i=0;i<sequence.size();++i) {
                    sequence[i].continue_movement=i+1<sequence.size();
                }
                if (launch) {
                    auto keys=plan_.launch_movement; keys.push_back(Key::interact);
                    Action activate{"Activate transport",std::move(keys),Time(100),{}};
                    sequence.insert(sequence.begin(),std::move(activate));
                    // A stationary camera turn preserves interaction range. Queue
                    // aiming, activation and flight without another prompt gate.
                    sequence.insert(sequence.begin(),plan_.launch_prepare.begin(),plan_.launch_prepare.end());
                }
                const auto remaining=plan_.timeout-(c.now-started_.now);
                const auto lifetime=plan_.sequence.empty()?std::min(remaining,input_lifetime(sequence)):remaining;
                pending_=tasks.submit(scope_,TaskClass::travel,std::move(sequence),lifetime);
                launched_=launch;
            } else pending_=tasks.submit(scope_,TaskClass::travel,{actions.at(index_)},Time(1000));
        }
    } catch (const std::exception& e) { fail(CheckpointFailure::input,e.what()); }
}
}
