#include "session.hpp"
#include <algorithm>
namespace hive {
namespace {
class FeatureTasks final : public TaskPort {
    TaskBroker& broker_;
    std::shared_ptr<const automation::NavigationPerception> navigation_;
    std::function<void(std::vector<Action>&)> prepare_;
public:
    FeatureTasks(TaskBroker& broker,std::shared_ptr<const automation::NavigationPerception> navigation,
        std::function<void(std::vector<Action>&)> prepare)
        :broker_(broker),navigation_(std::move(navigation)),prepare_(std::move(prepare)) {}
    bool acquire(ModuleScope scope) override { return broker_.acquire(scope); }
    void release(ModuleScope scope) override { broker_.release(scope); }
    std::string submit(ModuleScope scope,TaskClass kind,std::vector<Action> actions,Time lifetime) override {
        if (prepare_) prepare_(actions);
        if (navigation_) navigation_->input_submitted(actions);
        return broker_.submit(scope,kind,std::move(actions),monotonic_now(),lifetime);
    }
    std::vector<TaskResult> results(ModuleScope scope) override { return broker_.results(scope); }
    void cancel(ModuleScope scope) override { broker_.cancel(scope,monotonic_now()); }
};
}
void Session::configure_hotbar(std::shared_ptr<automation::HotbarSource> source) {
    std::lock_guard lock(mutex_); hotbar_=std::move(source);
}
game::ObservationNeeds Session::observation_needs() const {
    if (!feature_) return {};
    auto needs=feature_->observation_needs();
    const auto progress=feature_->progress();
    if (hotbar_ && hotbar_->needs_backpack() && progress.state==automation::State::running
        && progress.activity==WorkActivity::gathering && feature_grant_.capabilities.contains(Capability::interaction))
        needs.insert(game::ObservationKind::backpack);
    return needs;
}
Session::Session(Desktop& desktop): desktop_(desktop), observer_(desktop), runtime_(desktop), worker_([this]() {
    while (!exiting_.load()) {
        {
            std::lock_guard lock(mutex_);
            auto state=runtime_.status().state;
            if (state == RunState::running || state == RunState::paused) {
                try {
                    auto snapshot=desktop_.inspect(window_id_);
                    if (snapshot.focused && snapshot.pause_down && !pause_down_) ++pause_requests_;
                    pause_down_=snapshot.pause_down;
                    const auto now=monotonic_now();
                    const auto observations=feature_?observer_.latest(feature_->needs_movement_speed()).state:game::State{};
                    bool recovering=false;
                    const bool starting=feature_ && feature_->waiting_for_game_view() && !runtime_.has_work();
                    const bool pending_handoff=managed_ && continue_features_ && !feature_ && !runtime_.has_work();
                    if (starting && !snapshot.window.id) {
                        std::optional<Window> candidate;
                        bool ambiguous=false;
                        for (const auto& window:desktop_.windows()) if (window.process==active_window_.process) {
                            if (candidate) { ambiguous=true; break; }
                            candidate=window;
                        }
                        if (candidate && !ambiguous) snapshot=desktop_.inspect(candidate->id);
                    }
                    if (snapshot.escape_down) runtime_.stop("Emergency stop");
                    else if (!snapshot.input_allowed) runtime_.stop("Input permission lost");
                    else if (pending_handoff) {
                        if (!snapshot.focused) runtime_.pause(now);
                        recovering=true;
                    }
                    else if (starting && snapshot.window.id && snapshot.window.process==active_window_.process
                        && snapshot.window.bounds.valid() && snapshot.focused
                        && (snapshot.window.id!=active_window_.id || snapshot.window.bounds!=active_window_.bounds)) {
                        feature_->rebase_startup_window({snapshot.window,tasks_.generation(),now});
                        observer_.stop(); runtime_.stop("Updating startup window");
                        runtime_.start(snapshot.window,now,CompletionMode::managed);
                        active_window_=snapshot.window; window_id_=snapshot.window.id;
                        observer_.watch({active_window_,tasks_.generation(),now},observation_needs());
                        startup_window_missing_=Time(-1); recovering=true;
                    }
                    else if (starting && !snapshot.window.id &&
                        (startup_window_missing_<Time{} || now-startup_window_missing_<Time(30000))) {
                        if (startup_window_missing_<Time{}) startup_window_missing_=now;
                        recovering=true;
                    }
                    else if (window_recovery_) { recover_window(now); recovering=true; }
                    else if (feature_ && feature_->needs_client_rejoin() && snapshot.window.id && !snapshot.focused) runtime_.stop("Game focus lost before rejoin");
                    else if (feature_ && feature_->needs_client_rejoin() && begin_client_rejoin(now)) recovering=true;
                    else if (!snapshot.window.id && (begin_window_recovery(now) || begin_client_rejoin(now))) recovering=true;
                    else if (state==RunState::running && snapshot.focused && requested_rejoin(now)
                        && (starting || (feature_ && feature_->server_change_ready() && !runtime_.has_work()))
                        && begin_client_rejoin(now,true)) recovering=true;
                    else {
                        if (feature_) feature_->observe_movement({snapshot.window,tasks_.generation(),now},observations);
                        runtime_.tick(now,snapshot);
                    }
                    if (snapshot.window.id) startup_window_missing_=Time(-1);
                    if (!recovering && feature_) {
                        if (runtime_.status().state==RunState::running) {
                            feature_->update({snapshot.window,tasks_.generation(),monotonic_now()},observations);
                            runtime_.tick(monotonic_now(),snapshot);
                            observer_.watch({snapshot.window,tasks_.generation(),monotonic_now()},observation_needs());
                            observer_.request_text(feature_->text_regions());
                            observer_.request_labels(feature_->fixed_labels());
                            const auto progress=feature_->progress();
                            if (progress.state!=automation::State::running && !(continue_features_ && automation::can_handoff(progress))) { runtime_.stop(progress.message); observer_.stop(); }
                            if (feature_->can_replace_window()) {
                                if (!reconnect_windows_) {
                                    reconnect_windows_.emplace();
                                    for (const auto& window : desktop_.windows()) reconnect_windows_->emplace(window.id,window.process);
                                }
                            } else reconnect_windows_.reset();
                        } else if (runtime_.status().state==RunState::paused) {
                            feature_->pause(monotonic_now(),runtime_.status().reason); runtime_.stop(feature_->progress().message);
                        } else feature_->stop(runtime_.status().reason);
                    }
                    if (runtime_.status().state!=RunState::running) { window_recovery_.reset(); reconnect_windows_.reset(); client_rejoin_.reset(); initial_join_.reset(); observer_.stop(); }
                } catch (const std::exception& e) { runtime_.stop(e.what()); if (feature_) feature_->stop(e.what()); window_recovery_.reset(); reconnect_windows_.reset(); client_rejoin_.reset(); initial_join_.reset(); observer_.stop(); }
                catch (...) { runtime_.stop("Unexpected platform failure"); if (feature_) feature_->stop("Unexpected platform failure"); window_recovery_.reset(); reconnect_windows_.reset(); client_rejoin_.reset(); initial_join_.reset(); observer_.stop(); }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}) {}
Session::~Session() { exiting_.store(true); worker_.join(); stop(); }
void Session::start(Window window, std::vector<Job> jobs) {
    std::lock_guard lock(mutex_);
    const auto id=window.id;
    const auto now=monotonic_now();
    if (runtime_.status().state==RunState::running || runtime_.status().state==RunState::paused) throw std::logic_error("Stop the current session first");
    clear_feature();
    runtime_.start(window,now);
    window_id_=id;
    tasks_.reset(now); session_id_=tasks_.generation(); recovery_counts_=std::make_shared<RecoveryCounts>(); active_window_=window; managed_=false; continue_features_=false;
    try { for (auto& job : jobs) { job.due+=now; if (job.expires) *job.expires+=now; runtime_.enqueue(std::move(job)); } }
    catch (...) { runtime_.stop("Invalid session configuration"); throw; }
    if (observer_.has_detectors()) observer_.watch({window,tasks_.generation(),now});
}
void Session::pause() {
    std::lock_guard lock(mutex_);
    observer_.stop();
    if (feature_) { feature_->pause(monotonic_now()); runtime_.stop(feature_->progress().state==automation::State::completed?"Routine paused at a completed workflow":feature_->progress().message); }
    else runtime_.pause(monotonic_now());
    window_recovery_.reset(); reconnect_windows_.reset(); client_rejoin_.reset();
}
void Session::resume() {
    std::lock_guard lock(mutex_); const auto now=monotonic_now(); const auto snapshot=desktop_.inspect(window_id_);
    runtime_.resume(now,snapshot);
    if (runtime_.status().state==RunState::running && observer_.has_detectors()) observer_.watch({snapshot.window,tasks_.generation(),now});
}
void Session::stop(std::string reason) { std::lock_guard lock(mutex_); if (feature_) feature_->stop(reason); runtime_.stop(std::move(reason)); window_recovery_.reset(); reconnect_windows_.reset(); client_rejoin_.reset(); initial_join_.reset(); observer_.stop(); }
Status Session::status() { std::lock_guard lock(mutex_); return runtime_.status(); }
SessionSnapshot Session::snapshot() {
    std::lock_guard lock(mutex_);
    SessionSnapshot result{runtime_.status(),monotonic_now(),tasks_.generation(),{},pause_requests_,session_id_,active_window_};
    if (feature_) { result.feature=feature_->progress(); result.feature->module_id=feature_id_; }
    result.recovery=*recovery_counts_;
    result.server={rejoin_target_?rejoin_target_->uri():std::string{},joined_server_};
    result.reconnecting=client_rejoin_.has_value() || reconnect_windows_.has_value() || window_recovery_.has_value() || recovery_counts_->disconnected;
    return result;
}
void Session::pause_between_features() {
    std::lock_guard lock(mutex_);
    if (!continue_features_ || runtime_.status().state!=RunState::running || runtime_.has_work()
        || (feature_ && !automation::can_handoff(feature_->progress())))
        throw std::logic_error("Finish the current workflow before pausing the routine");
    clear_feature();
    runtime_.pause(monotonic_now());
}
std::uint64_t Session::generation() { std::lock_guard lock(mutex_); return tasks_.generation(); }
void Session::start_managed(Window window) {
    std::lock_guard lock(mutex_); const auto now=monotonic_now();
    if (runtime_.status().state==RunState::running || runtime_.status().state==RunState::paused) throw std::logic_error("Stop the current session first");
    clear_feature(); startup_window_missing_=Time(-1);
    const auto observed=desktop_.inspect(window.id);
    if (observed.window.id==window.id && observed.window.process==window.process && observed.window.bounds.valid() && observed.focused)
        window=observed.window;
    runtime_.start(window,now,CompletionMode::managed); window_id_=window.id; tasks_.reset(now); managed_=true; continue_features_=true;
    active_window_=window; session_id_=tasks_.generation(); recovery_counts_=std::make_shared<RecoveryCounts>();
    rejoin_schedule_.arm(wall_time());
    if (observer_.has_detectors()) observer_.watch({window,tasks_.generation(),now});
}
void Session::clear_feature(bool stop_observer) {
    if (stop_observer) observer_.stop();
    feature_.reset(); feature_tasks_.reset();
    feature_id_.clear();
    feature_grant_={}; window_recovery_.reset(); reconnect_windows_.reset(); client_rejoin_.reset();
    if (feature_scope_) { tasks_.revoke(*feature_scope_,monotonic_now()); feature_scope_.reset(); }
}
void Session::configure_rejoin(std::optional<game::JoinTarget> target) {
    std::lock_guard lock(mutex_);
    const auto state=runtime_.status().state;
    if (state==RunState::running || state==RunState::paused) throw std::logic_error("Stop before changing the rejoin server");
    rejoin_target_=std::move(target);
}
void Session::configure_connection_recovery(automation::ProgramFactory reconnect,automation::ProgramFactory replacement) {
    std::lock_guard lock(mutex_);
    const auto state=runtime_.status().state;
    if (state==RunState::running || state==RunState::paused) throw std::logic_error("Stop before changing connection recovery");
    if (bool(reconnect)!=bool(replacement)) throw std::invalid_argument("Connection recovery requires both connection factories");
    connection_reconnect_=std::move(reconnect); connection_replacement_=std::move(replacement);
}
void Session::configure_conversion(std::shared_ptr<game::HiveConversion> conversion) {
    std::lock_guard lock(mutex_);
    const auto state=runtime_.status().state;
    if(state==RunState::running || state==RunState::paused) throw std::runtime_error("Stop the session before changing conversion settings");
    hive_conversion_=std::move(conversion);
}
void Session::configure_speed(std::shared_ptr<const game::MovementSpeedSource> source) {
    std::lock_guard lock(mutex_);
    const auto state=runtime_.status().state;
    if (state==RunState::running || state==RunState::paused) throw std::logic_error("Stop before changing movement speed");
    observer_.stop(); observer_.configure_speed(std::move(source));
}
void Session::configure_navigation(automation::NavigationServices navigation) {
    std::lock_guard lock(mutex_);
    const auto state=runtime_.status().state;
    if (state==RunState::running || state==RunState::paused) throw std::logic_error("Stop before changing navigation services");
    if (bool(navigation.routes)!=bool(navigation.perception)) throw std::invalid_argument("Navigation requires routes and perception together");
    navigation_=std::move(navigation);
}
std::map<std::string,std::shared_ptr<const game::DetectionSource>> Session::configure_detectors(std::vector<game::DetectorConfig> detectors) {
    std::lock_guard lock(mutex_);
    const auto state=runtime_.status().state;
    if (state==RunState::running || state==RunState::paused) throw std::logic_error("Stop before changing detectors");
    return observer_.configure(std::move(detectors));
}
void Session::start_feature(Window window,automation::Launch launch) {
    std::lock_guard lock(mutex_);
    if (runtime_.status().state==RunState::running || runtime_.status().state==RunState::paused) throw std::logic_error("Stop the current session first");
    clear_feature(); const auto now=monotonic_now();
    runtime_.start(window,now,CompletionMode::managed); window_id_=window.id; tasks_.reset(now); managed_=true; continue_features_=false;
    active_window_=window; session_id_=tasks_.generation(); recovery_counts_=std::make_shared<RecoveryCounts>();
    launch_feature(window,std::move(launch),now);
}
void Session::run_feature(automation::Launch launch) {
    std::lock_guard lock(mutex_);
    if (!continue_features_ || !managed_ || runtime_.status().state!=RunState::running)
        throw std::logic_error("Start a managed session before handing off work");
    if (feature_ && !automation::can_handoff(feature_->progress()))
        throw std::logic_error("The current feature must finish before handing off work");
    Window window;
    try {
        const auto now=monotonic_now(); auto snapshot=desktop_.inspect(window_id_);
        if (!feature_ && !runtime_.has_work() && !snapshot.window.id) {
            std::optional<Window> candidate;
            bool ambiguous=false;
            for (const auto& window:desktop_.windows()) if (window.process==active_window_.process) {
                if (candidate) { ambiguous=true; break; }
                candidate=window;
            }
            if (candidate && !ambiguous) snapshot=desktop_.inspect(candidate->id);
        }
        if (!feature_ && !runtime_.has_work() && snapshot.window.id && snapshot.window.process==active_window_.process
            && snapshot.window.bounds.valid() && snapshot.focused
            && (snapshot.window.id!=active_window_.id || snapshot.window.bounds!=active_window_.bounds)) {
            observer_.stop(); runtime_.stop("Updating startup window");
            runtime_.start(snapshot.window,now,CompletionMode::managed);
            active_window_=snapshot.window; window_id_=snapshot.window.id;
        }
        if (snapshot.escape_down) runtime_.stop("Emergency stop");
        else runtime_.tick(now,snapshot);
        if (runtime_.status().state!=RunState::running) throw std::runtime_error("Game focus or window changed before handoff");
        clear_feature(false);
        window=snapshot.window;
    } catch (...) { clear_feature(); runtime_.stop("Feature handoff failed"); throw; }
    launch_feature(window,std::move(launch),monotonic_now());
}
void Session::launch_feature(Window window,automation::Launch launch,Time now) {
    try {
        std::set<Capability> capabilities;
        for (auto permission : launch.permissions) {
            if (permission==automation::Permission::movement) capabilities.insert(Capability::movement);
            else if (permission==automation::Permission::tool || permission==automation::Permission::interact) {
                capabilities.insert(Capability::interaction);
                if (permission==automation::Permission::interact) capabilities.insert(Capability::pointer);
            }
            else throw std::invalid_argument("Unsupported feature permission");
        }
        feature_id_=launch.module_id;
        feature_grant_={std::move(launch.module_id),std::move(capabilities),{TaskClass::activity,TaskClass::travel,TaskClass::interaction}};
        feature_scope_=tasks_.grant(feature_grant_);
        feature_tasks_=std::make_unique<FeatureTasks>(tasks_,navigation_.perception,[this](std::vector<Action>& actions) {
            if (!hotbar_ || !feature_ || !feature_grant_.capabilities.contains(Capability::interaction)) return;
            const auto progress=feature_->progress();
            const ObservationContext context{active_window_,tasks_.generation(),monotonic_now()};
            automation::HotbarContext hotbar{context,progress.activity,progress.field};
            if (hotbar_->needs_backpack() && progress.activity==WorkActivity::gathering) {
                const auto state=observer_.latest().state;
                if (state.backpack_percent.has_value(context,Time(1500))) hotbar.backpack=state.backpack_percent.value;
            }
            automation::attach_hotbar(actions,*hotbar_,hotbar);
        });
        feature_=std::make_unique<automation::Runner>(std::move(launch.program),std::move(launch.permissions),*feature_tasks_,*feature_scope_,navigation_);
        feature_->configure_reporting([this](const automation::ReportActivity& event) {
            if (activity_events_.size()>=8) activity_events_.erase(activity_events_.begin());
            std::shared_ptr<const Frame> frame;
            if (event.image_region || event.status.kind==ActivityKind::failure) {
                frame=observer_.latest_frame();
                const auto now=monotonic_now();
                if (frame && (!frame->valid() || frame->window.id!=active_window_.id || frame->window.process!=active_window_.process
                    || frame->window.bounds!=active_window_.bounds || frame->captured>now || now-frame->captured>Time(3000))) frame.reset();
            }
            activity_events_.push_back({event.status,std::move(frame),event.image_region});
        });
        feature_->configure_conversion(hive_conversion_); feature_->configure_statistics(recovery_counts_);
        feature_->configure_connection_recovery(connection_reconnect_,connection_replacement_);
        feature_->allow_client_rejoin(rejoin_target_.has_value() && desktop_.client_lifecycle_available());
        const auto joined=std::exchange(initial_join_,{});
        const bool joining=joined && joined->first.process==window.process
            && now>=joined->second && now-joined->second<automation::game_loading_timeout;
        feature_->start({window,tasks_.generation(),now},joining);
        next_server_read_={};
        if (managed_ && requested_rejoin(now)) begin_client_rejoin(now,true);
        if (!client_rejoin_) observer_.watch({window,tasks_.generation(),now},observation_needs());
    } catch (...) { clear_feature(); if (!continue_features_) runtime_.stop("Feature could not start"); throw; }
}
bool Session::yield_feature(const std::string& id) {
    std::lock_guard lock(mutex_);
    return feature_ && feature_id_==id && feature_->request_yield();
}
void Session::stop_feature(const std::string& module_id,const std::string& reason) {
    std::lock_guard lock(mutex_);
    if (!feature_ || feature_id_!=module_id) return;
    const auto state=feature_->progress().state;
    if (state!=automation::State::running && state!=automation::State::paused) return;
    feature_->fail(reason);
    if (!continue_features_ || !automation::can_handoff(feature_->progress())) runtime_.stop(reason);
    window_recovery_.reset(); reconnect_windows_.reset(); client_rejoin_.reset();
    observer_.stop();
}
game::ObservedState Session::observations() { return observer_.latest(false); }
std::vector<ActivityEvent> Session::take_activity_events() {
    std::lock_guard lock(mutex_); std::vector<ActivityEvent> result; result.swap(activity_events_); return result;
}
std::shared_ptr<const Frame> Session::latest_frame() const { return observer_.latest_frame(); }
std::optional<automation::Progress> Session::feature_progress() {
    std::lock_guard lock(mutex_); if (!feature_) return {};
    auto progress=feature_->progress(); progress.module_id=feature_id_; return progress;
}
ModuleScope Session::grant(ModuleGrant grant) {
    std::lock_guard lock(mutex_);
    if (!managed_ || window_recovery_ || runtime_.status().state!=RunState::running) throw std::logic_error("Start a managed session before registering modules");
    return tasks_.grant(std::move(grant));
}
void Session::revoke(ModuleScope scope) { std::lock_guard lock(mutex_); tasks_.revoke(scope,monotonic_now()); }
bool Session::acquire(ModuleScope scope) { std::lock_guard lock(mutex_); return !window_recovery_ && tasks_.acquire(scope); }
void Session::release(ModuleScope scope) { std::lock_guard lock(mutex_); tasks_.release(scope); }
std::string Session::submit(ModuleScope scope,TaskClass kind,std::vector<Action> actions,Time lifetime) {
    std::lock_guard lock(mutex_);
    if (window_recovery_) throw std::logic_error("Game input is suspended during window recovery");
    if (navigation_.perception) navigation_.perception->input_submitted(actions);
    return tasks_.submit(scope,kind,std::move(actions),monotonic_now(),lifetime);
}
std::vector<TaskResult> Session::results(ModuleScope scope) { std::lock_guard lock(mutex_); return tasks_.results(scope); }
void Session::cancel(ModuleScope scope) { std::lock_guard lock(mutex_); tasks_.cancel(scope,monotonic_now()); }
}
