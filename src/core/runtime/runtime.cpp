#include "runtime.hpp"
#include "pointer_tasks.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace hive {
Runtime::~Runtime() { stop_pointer(); release(); }
bool Runtime::has_work() const { return active_.has_value() || !queue_.empty() || tap_release_.has_value() || (pointer_tasks_ && pointer_tasks_->engaged()); }
bool Runtime::stop_pointer() noexcept {
    if (!pointer_tasks_) return true;
    try { pointer_tasks_->stop(input_,last_tick_); pointer_tasks_.reset(); return true; }
    catch (...) { status_.state=RunState::faulted; status_.reason="Pointer release failed"; return false; }
}
bool Runtime::release(const std::set<Key>& keep) noexcept {
    bool released=true;
    for (auto it=held_.begin(); it!=held_.end();) {
        if (keep.contains(*it)) { ++it; continue; }
        try { input_.key(*it, false); it=held_.erase(it); }
        catch (...) { ++it; released=false; }
    }
    if (!released) {
        status_.state = RunState::faulted;
        status_.reason = "Input release failed; stop and check held keys";
    }
    if (tap_release_ && !held_.contains(tap_release_->key)) tap_release_.reset();
    return released;
}
void Runtime::fault(std::string reason) {
    status_.state = RunState::faulted;
    status_.reason = std::move(reason);
    stop_pointer(); release();
    if (active_) finish(*active_,TaskOutcome::faulted,last_tick_,status_.reason);
    for (const auto& pending : queue_) finish(pending,TaskOutcome::faulted,last_tick_,status_.reason);
    active_.reset(); queue_.clear();
}
void Runtime::start(Window window, Time now, CompletionMode mode) {
    if (status_.state == RunState::running || status_.state == RunState::paused)
        throw std::logic_error("Stop the current session before starting another");
    if (!window.id || !window.bounds.valid()) throw std::invalid_argument("Select a valid game window");
    const bool pointer_released=stop_pointer();
    if (!release() || !pointer_released) throw std::runtime_error("Could not release previous inputs");
    queue_.clear(); active_.reset(); results_.clear(); mode_=mode;
    window_ = std::move(window); last_tick_ = now; interrupted_interaction_ = false;
    status_ = {}; status_.state = RunState::running;
}
void Runtime::enqueue(Job job) {
    if (status_.state != RunState::running && status_.state != RunState::paused)
        throw std::logic_error("Start a session before submitting work");
    if (job.id.empty() || job.actions.empty() || job.actions.size() > 10000 || job.priority < 0 || job.priority > 100)
        throw std::invalid_argument("Invalid job");
    if (job.due<Time::zero() || (job.expires && *job.expires<=job.due)) throw std::invalid_argument("Invalid job deadline");
    if (active_ && active_->job.id == job.id) throw std::invalid_argument("Duplicate job ID");
    for (const auto& pending : queue_) if (pending.job.id == job.id) throw std::invalid_argument("Duplicate job ID");
    for (const auto& a : job.actions) {
        if (a.concurrent_pointer && (a.concurrent_pointer->scope().id!=job.owner || !allows_concurrent_pointer(a)))
            throw std::invalid_argument("Concurrent pointer work requires compatible input by the same owner");
        if (a.duration <= Time::zero() || a.duration > std::chrono::minutes(10)) throw std::invalid_argument("Action duration must be between 1 ms and 10 minutes");
        if (a.pacing && (!job.expires || a.duration>Time(250) || a.held.empty() || a.pointer || a.pointer_to || a.scroll
            || !std::any_of(a.held.begin(),a.held.end(),[](Key key) { return key>=Key::forward && key<=Key::right; })
            || !std::all_of(a.held.begin(),a.held.end(),[](Key key) { return (key>=Key::forward && key<=Key::right) || key==Key::primary || key==Key::jump; })))
            throw std::invalid_argument("Paced actions require bounded walking and a deadline");
        if (a.tap_count<1 || a.tap_count>8 || (!a.tap && a.tap_count!=1))
            throw std::invalid_argument("Invalid key tap count");
        if (a.tap_duration<Time{} || (a.tap_duration>Time{} && (!a.tap
            || (!hotbar_key(*a.tap) && *a.tap!=Key::respawn && *a.tap!=Key::confirm)
            || a.tap_count!=1 || a.tap_duration>Time(100) || a.tap_duration>a.duration)))
            throw std::invalid_argument("Invalid held key tap");
        if (a.tap) {
            const bool jump=*a.tap==Key::jump && std::all_of(a.held.begin(),a.held.end(),[](Key key) {
                return (key>=Key::forward && key<=Key::right) || camera_key(key);
            });
            const bool turn=(*a.tap==Key::rotate_left || *a.tap==Key::rotate_right)
                && std::all_of(a.held.begin(),a.held.end(),[](Key key) { return key>=Key::forward && key<=Key::right; });
            const bool menu=menu_key(*a.tap) && a.held.empty() && !a.continue_movement && a.tap_count==1;
            const bool hotbar=hotbar_key(*a.tap) && a.tap_count==1 && std::all_of(a.held.begin(),a.held.end(),[](Key key) {
                return (key>=Key::forward && key<=Key::right) || key==Key::primary;
            });
            if ((!jump && !menu && !turn && !hotbar) || a.pointer || a.pointer_to || a.scroll || (a.pacing && !hotbar))
                throw std::invalid_argument("Invalid key tap");
        }
        for (auto key : a.held) if (key < Key::forward || key > Key::select_all) throw std::invalid_argument("Unsupported key");
        if (a.pointer && (!std::isfinite(a.pointer->x) || !std::isfinite(a.pointer->y)
            || a.pointer->x < 0 || a.pointer->y < 0 || a.pointer->x >= 1 || a.pointer->y >= 1))
            throw std::invalid_argument("Pointer coordinates must be within the normalized window bounds");
        if (a.scroll<-8 || a.scroll>8 || (a.scroll && (!a.pointer || !a.held.empty() || a.pointer_to)))
            throw std::invalid_argument("Scroll actions need a pointer position and no held keys");
        if (a.pointer_to && (!a.pointer || a.held!=std::vector<Key>{Key::primary} || !std::isfinite(a.pointer_to->x) || !std::isfinite(a.pointer_to->y)
            || a.pointer_to->x<0 || a.pointer_to->y<0 || a.pointer_to->x>=1 || a.pointer_to->y>=1))
            throw std::invalid_argument("Drag actions need bounded endpoints and the primary button");
    }
    const auto motion=[](const Action& action) {
        const bool walking=std::any_of(action.held.begin(),action.held.end(),[](Key k) { return k>=Key::forward && k<=Key::right; });
        return !action.pointer && !action.pointer_to && !action.scroll
            && std::all_of(action.held.begin(),action.held.end(),[&](Key key) {
                return (key>=Key::forward && key<=Key::jump) || key==Key::primary || (walking && action.duration<=Time(100) && (key==Key::rotate_left || key==Key::rotate_right));
            });
    };
    for (std::size_t i=0;i<job.actions.size();++i) if (job.actions[i].continue_movement
        && (i+1==job.actions.size() || !motion(job.actions[i]) || !motion(job.actions[i+1])))
        throw std::invalid_argument("Continuous movement requires adjacent movement actions");
    if (queue_.size() >= 128 || results_.size()+queue_.size()+(active_?1:0)>=256) throw std::invalid_argument("Job queue or unread results are full");
    queue_.push_back({std::move(job)});
    status_.reason.clear();
}
bool Runtime::guard(const Snapshot& snapshot) {
    if (snapshot.escape_down) { stop("Emergency stop"); return false; }
    if (snapshot.window.id != window_.id || snapshot.window.process != window_.process || snapshot.window.bounds != window_.bounds) { fault("Game window moved, resized, or closed"); return false; }
    if (!snapshot.input_allowed) { fault("Input permission lost"); return false; }
    if (!snapshot.focused) { pause(last_tick_); if (status_.state==RunState::paused) status_.reason="Paused: Roblox lost focus"; return false; }
    return true;
}
void Runtime::choose(Time now) {
    if (!active_ && tap_release_) return;
    std::erase_if(queue_, [this,now](const Pending& p) {
        if (!p.job.expires || now<*p.job.expires) return false;
        finish(p,TaskOutcome::expired,now,"Task deadline passed"); return true;
    });
    auto best = queue_.end();
    for (auto it=queue_.begin(); it!=queue_.end(); ++it) {
        if (it->job.due > now) continue;
        if (best == queue_.end() || it->job.priority > best->job.priority
            || (it->job.priority == best->job.priority && it->job.due < best->job.due)) best = it;
    }
    if (best == queue_.end() || (active_ && best->job.priority <= active_->job.priority)) return;
    if (!release()) { fault(status_.reason); return; }
    Pending next = std::move(*best); queue_.erase(best);
    if (active_) queue_.push_back(std::move(*active_));
    active_ = std::move(next);
}
void Runtime::tick(Time now, const Snapshot& snapshot) {
    if (status_.state != RunState::running) return;
    if (now < last_tick_) { fault("Runtime clock moved backwards"); return; }
    last_tick_ = now;
    if (!guard(snapshot)) return;
    try {
        if (tap_release_ && now>=tap_release_->until) {
            input_.key(tap_release_->key,false); held_.erase(tap_release_->key); tap_release_.reset();
        }
        std::string continuation_job;
        Time continuation_end{};
        if (pointer_tasks_) {
            const bool compatible=!active_ || active_->job.actions.at(active_->index).concurrent_pointer==pointer_tasks_;
            const auto began=std::chrono::steady_clock::now();
            pointer_tasks_->tick(input_,window_,now,compatible);
            now+=std::chrono::duration_cast<Time>(std::chrono::steady_clock::now()-began);
        }
        if (active_ && active_->job.expires && now >= *active_->job.expires) {
            if (!release()) { fault(status_.reason); return; }
            finish(*active_,TaskOutcome::expired,now,"Task deadline passed");
            active_.reset();
        }
        if (active_ && active_->begun) {
            const auto& action=active_->job.actions.at(active_->index);
            if (action.pointer_to) {
                const auto settle=std::min(Time(30),action.duration/5);
                const auto part=std::clamp(double((now-active_->segment_start-settle).count())/(action.duration-2*settle).count(),0.0,1.0);
                const auto from=*action.pointer,to=*action.pointer_to;
                input_.pointer({window_.bounds.x+(from.x+(to.x-from.x)*part)*window_.bounds.width,
                    window_.bounds.y+(from.y+(to.y-from.y)*part)*window_.bounds.height});
            }
            if (now < end_) return;
            Time remaining{};
            if (action.pacing) {
                remaining=action.pacing->next(active_->segment_start,now);
                if (remaining<Time{} || remaining>action.duration) throw std::runtime_error("Invalid movement pacing interval");
            }
            std::set<Key> keep;
            if (remaining>Time{}) keep=held_;
            else if (action.continue_movement) {
                const auto& next=active_->job.actions.at(active_->index+1);
                for (const auto key : action.held) if (((key>=Key::forward && key<=Key::jump) || key==Key::primary)
                    && std::find(next.held.begin(),next.held.end(),key)!=next.held.end()) keep.insert(key);
                if (!action.pacing && !next.pacing && action.held==next.held && keep.size()==action.held.size()) {
                    continuation_job=active_->job.id;
                    continuation_end=end_+next.duration;
                }
            }
            if (tap_release_) keep.insert(tap_release_->key);
            if (!release(keep)) { fault(status_.reason); return; }
            active_->execution_time+=now-active_->segment_start;
            active_->begun = false; active_->remaining = remaining;
            if (remaining==Time{}) { ++active_->index; ++status_.completed_actions; }
            if (active_->index == active_->job.actions.size()) { finish(*active_,TaskOutcome::executed,now); active_.reset(); }
        }
        choose(now);
        if (status_.state!=RunState::running) return;
        if (!active_) {
            status_.job.clear(); status_.action.clear();
            if (queue_.empty() && !tap_release_ && (!pointer_tasks_ || !pointer_tasks_->engaged())) {
                if (mode_==CompletionMode::finite) { status_.state = RunState::completed; status_.reason = "Session completed"; }
                else status_.reason="Waiting for workflow";
            }
            return;
        }
        auto& pending = *active_;
        const auto& action = pending.job.actions.at(pending.index);
        if (pointer_tasks_!=action.concurrent_pointer) {
            if (pointer_tasks_ && pointer_tasks_->engaged()) return;
            pointer_tasks_=action.concurrent_pointer;
        }
        auto duration=pending.remaining>Time{}?pending.remaining:action.duration;
        if (action.pacing) {
            duration=action.pacing->next(now,now);
            if (duration<Time(1) || duration>action.duration) throw std::runtime_error("Invalid initial movement pacing interval");
        }
        status_.job = pending.job.id; status_.action = action.label;
        pending.started=true;
        if (action.pointer) {
            const auto began=std::chrono::steady_clock::now();
            input_.pointer({window_.bounds.x+action.pointer->x*window_.bounds.width, window_.bounds.y+action.pointer->y*window_.bounds.height});
            now+=std::chrono::duration_cast<Time>(std::chrono::steady_clock::now()-began);
        }
        for (auto key : action.held) if (held_.insert(key).second) input_.key(key, true);
        if (action.tap && pending.remaining==Time{}) for (unsigned i=0;i<action.tap_count;++i) {
            held_.insert(*action.tap);
            input_.key(*action.tap,true);
            if (action.tap_duration>Time{}) tap_release_=HeldTap{*action.tap,now+action.tap_duration,pending.job.owner};
            else { input_.key(*action.tap,false); held_.erase(*action.tap); }
        }
        if (action.scroll) input_.scroll(action.scroll);
        end_ = continuation_job==pending.job.id?continuation_end:now+duration;
        pending.begun = true;
        pending.segment_start = now;
    } catch (const std::exception& error) { fault(error.what()); }
    catch (...) { fault("Unexpected input failure"); }
}
void Runtime::pause(Time now) {
    if (status_.state != RunState::running) return;
    if (active_ && active_->begun) {
        interrupted_interaction_ = std::any_of(active_->job.actions.begin()+active_->index,active_->job.actions.end(),[](const Action& action) {
            return action.tap || action.pacing || action.scroll || std::any_of(action.held.begin(),action.held.end(),[](Key key) { return key==Key::primary || key==Key::interact || hotbar_key(key); });
        });
        active_->remaining = std::max(Time(1), end_-now);
        active_->execution_time+=now-active_->segment_start;
        active_->begun = false;
    }
    const bool pointer_released=stop_pointer();
    if (!release() || !pointer_released) { fault(status_.reason); return; }
    status_.state = RunState::paused; status_.reason = "Paused";
}
void Runtime::resume(Time now, const Snapshot& snapshot) {
    if (status_.state != RunState::paused || !guard(snapshot)) return;
    if (interrupted_interaction_) { fault("Interrupted operation needs a fresh game-state check before restarting"); return; }
    last_tick_ = now; status_.state = RunState::running; status_.reason.clear();
}
void Runtime::stop(std::string reason) {
    status_.state = RunState::stopped; status_.reason = std::move(reason);
    const bool pointer_released=stop_pointer();
    const bool released=release() && pointer_released;
    if (active_) finish(*active_,released?TaskOutcome::cancelled:TaskOutcome::faulted,last_tick_,status_.reason);
    for (const auto& pending : queue_) finish(pending,released?TaskOutcome::cancelled:TaskOutcome::faulted,last_tick_,status_.reason);
    active_.reset(); queue_.clear();
    status_.job.clear(); status_.action.clear();
}
Status Runtime::status() const { auto copy=status_; copy.queued_jobs=queue_.size(); return copy; }
void Runtime::finish(const Pending& p,TaskOutcome outcome,Time now,std::string reason) {
    results_.push_back({p.job.id,p.job.owner,outcome,now,p.index,p.started,std::move(reason),p.execution_time+(p.begun?std::max(Time::zero(),now-p.segment_start):Time::zero())});
}
void Runtime::cancel_owner(std::uint64_t owner,Time now) {
    const auto began=std::chrono::steady_clock::now();
    if (tap_release_ && tap_release_->owner==owner) {
        auto keep=held_; keep.erase(tap_release_->key);
        if (!release(keep)) { fault(status_.reason); return; }
    }
    if (pointer_tasks_ && pointer_tasks_->scope().id==owner && !stop_pointer()) { fault(status_.reason); return; }
    if (active_ && active_->job.owner==owner) {
        if (!release()) { fault(status_.reason); return; }
        const auto stopped=now+std::chrono::duration_cast<Time>(std::chrono::steady_clock::now()-began);
        if (active_->begun) if (const auto& pacing=active_->job.actions.at(active_->index).pacing) {
            try { pacing->next(active_->segment_start,stopped); }
            catch (const std::exception& error) { fault(error.what()); return; }
        }
        finish(*active_,TaskOutcome::cancelled,stopped,"Module work cancelled"); active_.reset();
        status_.job.clear(); status_.action.clear(); interrupted_interaction_=false;
    }
    std::erase_if(queue_,[&](const Pending& p) {
        if (p.job.owner!=owner) return false;
        finish(p,TaskOutcome::cancelled,now,"Module work cancelled"); return true;
    });
}
std::vector<TaskResult> Runtime::take_results(std::uint64_t owner) {
    std::vector<TaskResult> result;
    std::erase_if(results_,[&](const TaskResult& r) { if (r.owner!=owner) return false; result.push_back(r); return true; });
    return result;
}
}
