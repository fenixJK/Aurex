#include "session.hpp"
#include "game/detection/connection.hpp"
#include <utility>

namespace hive {
void Session::configure_rejoin_schedule(int hours,int minute) {
    std::lock_guard lock(mutex_);
    const auto state=runtime_.status().state;
    if (state==RunState::running || state==RunState::paused) throw std::logic_error("Stop before changing the reconnect schedule");
    rejoin_schedule_.configure(hours,minute,wall_time());
}
void Session::configure_server_requests(std::shared_ptr<const game::RequestSource> requests) {
    std::lock_guard lock(mutex_); server_requests_=std::move(requests); requested_server_.reset(); next_server_read_={};
}
bool Session::read_server_request(Time now) {
    if (!server_requests_ || now<next_server_read_) return false;
    next_server_read_=now+Time(250);
    requested_server_=game::server_goal(server_requests_->channels(now,tasks_.generation()),now,tasks_.generation());
    return true;
}
std::optional<game::JoinTarget> Session::rejoin_target(Time now) const {
    return requested_server_ && requested_server_->expires>now?std::optional(requested_server_->target):rejoin_target_;
}
bool Session::requested_rejoin(Time now) {
    if (!rejoin_target_ || !connection_replacement_ || !desktop_.client_lifecycle_available()
        || !feature_grant_.capabilities.contains(Capability::pointer)) return false;
    const bool refreshed=read_server_request(now);
    const bool scheduled=continue_features_ && rejoin_schedule_.due(wall_time());
    const bool server_change=requested_server_ && requested_server_->expires>now && requested_server_->key!=joined_server_;
    const bool pending=server_change || scheduled;
    if (pending && (scheduled || (refreshed && requested_server_->interrupt)) && feature_ && !feature_->waiting_for_game_view()) feature_->request_yield();
    return pending;
}

ClientRejoin Session::connect_client(Time now,Window replace) {
    std::lock_guard lock(mutex_);
    const auto state=runtime_.status().state;
    if(state==RunState::running || state==RunState::paused) throw std::logic_error("Stop before opening a new client");
    read_server_request(now); const auto target=rejoin_target(now);
    if(!target || !desktop_.client_lifecycle_available()) throw std::runtime_error("Automatic game launch is unavailable");
    if(requested_server_ && requested_server_->expires>now) joined_server_=requested_server_->key;
    return ClientRejoin(*target,std::move(replace),desktop_.client_processes(),now);
}
bool Session::begin_window_recovery(Time now) {
    if (!feature_ || !reconnect_windows_ || !active_window_.process || !feature_->suspend_window(now)) return false;
    observer_.stop();
    runtime_.stop("Waiting for Roblox's replacement window");
    if (runtime_.status().state==RunState::faulted) throw std::runtime_error("Could not release input before replacing the game window");
    runtime_.start(active_window_,now,CompletionMode::managed);
    window_recovery_=WindowRecovery{now+Time(120000),now};
    return true;
}
bool Session::begin_client_rejoin(Time now,bool requested) {
    read_server_request(now); const auto target=rejoin_target(now);
    if (!feature_ || !target || !desktop_.client_lifecycle_available()) return false;
    if (!requested && !feature_->needs_client_rejoin() && !feature_->expects_reconnect() && desktop_.client_running(active_window_.process)) return false;
    const auto observed=observer_.latest(false).state;
    const ObservationContext context{active_window_,tasks_.generation(),now};
    const bool home=observed.text.has_value(context,Time(2000)) && game::client_home_screen(observed.text.value)
        && !(observed.game_hud.has_value(context,Time(2000)) && observed.game_hud.value);
    if (!feature_->prepare_client_rejoin(now)) return false;
    observer_.stop(); runtime_.stop("Preparing Roblox rejoin");
    if (runtime_.status().state==RunState::faulted) throw std::runtime_error("Could not release input before relaunching Roblox");
    joining_server_=requested_server_ && requested_server_->expires>now?requested_server_->key:std::string{};
    rejoin_schedule_.arm(wall_time());
    const RejoinLimits limits;
    client_rejoin_.emplace(*target,active_window_,desktop_.client_processes(),now,limits,
        home?RejoinMode::join_existing:RejoinMode::restart);
    runtime_.start(active_window_,now,CompletionMode::managed);
    window_recovery_=WindowRecovery{now+limits.close+limits.window+limits.focus,now};
    return true;
}
void Session::recover_window(Time now) {
    if (!feature_->wait_window(now,client_rejoin_?client_rejoin_->status():std::string{}) || now>=window_recovery_->deadline) {
        if (feature_->progress().state==automation::State::running) feature_->stop("Timed out waiting for Roblox's replacement window");
        runtime_.stop(feature_->progress().message); window_recovery_.reset(); reconnect_windows_.reset(); client_rejoin_.reset(); return;
    }
    if (client_rejoin_) {
        if (auto window=client_rejoin_->update(desktop_,now)) adopt_window(*window,now);
        return;
    }
    if (rejoin_target_ && desktop_.client_lifecycle_available() && !desktop_.client_running(active_window_.process)) {
        if (begin_client_rejoin(now)) return;
    }
    if (now<window_recovery_->next_scan) return;
    window_recovery_->next_scan=now+Time(250);
    std::optional<Window> candidate;
    bool ambiguous=false;
    for (const auto& window : desktop_.windows()) {
        if (!window.id || !window.bounds.valid() || window.process!=active_window_.process
            || reconnect_windows_->contains({window.id,window.process})) continue;
        const double area=window.bounds.width*window.bounds.height;
        const double best=candidate?candidate->bounds.width*candidate->bounds.height:0;
        if (!candidate || area>best) { candidate=window; ambiguous=false; }
        else if (window.id!=candidate->id && area==best) ambiguous=true;
    }
    if (!candidate || ambiguous) return;
    const auto observed=desktop_.inspect(candidate->id);
    if (observed.escape_down) { feature_->stop("Emergency stop"); runtime_.stop("Emergency stop"); return; }
    if (!observed.input_allowed) { feature_->stop("Input permission lost"); runtime_.stop("Input permission lost"); return; }
    if (!observed.focused || observed.window.id!=candidate->id || observed.window.process!=candidate->process
        || observed.window.bounds!=candidate->bounds) return;
    adopt_window(observed.window,now);
}
void Session::adopt_window(const Window& window,Time now) {
    runtime_.stop("Replacing the game window");
    tasks_.reset(now); feature_scope_=tasks_.grant(feature_grant_);
    runtime_.start(window,now,CompletionMode::managed);
    feature_->replace_window({window,tasks_.generation(),now},*feature_scope_);
    active_window_=window; window_id_=active_window_.id;
    if (client_rejoin_) joined_server_=std::exchange(joining_server_,{});
    window_recovery_.reset(); reconnect_windows_.reset(); client_rejoin_.reset();
    if (feature_->progress().state!=automation::State::running) { runtime_.stop(feature_->progress().message); return; }
    observer_.watch({active_window_,tasks_.generation(),now},feature_->observes_game(),bool(connection_reconnect_),feature_->observes_interaction());
}
}
