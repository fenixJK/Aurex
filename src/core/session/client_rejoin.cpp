#include "client_rejoin.hpp"

namespace hive {
ClientRejoin::ClientRejoin(game::JoinTarget target,Window old,const std::vector<std::int64_t>& existing,Time now,RejoinLimits limits,RejoinMode mode)
    :target_(std::move(target)),limits_(limits),old_(std::move(old)),deadline_(now+limits.close),next_(now),last_(now) {
    if ((old_.id && old_.process<=0) || (!old_.id && old_.process) || limits.close<=Time{} || limits.window<=Time{} || limits.focus<=Time{} || limits.poll<=Time{}
        || limits.retry<=Time{} || !limits.attempts || limits.attempts>3)
        throw std::invalid_argument("Invalid client recovery configuration");
    if(old_.id) {
        for (const auto process:existing) if (process!=old_.process) excluded_.insert(process);
        if (mode==RejoinMode::restart) excluded_.insert(old_.process);
    }
    if (!old_.id || mode==RejoinMode::join_existing) { stage_=Stage::launching; deadline_=now+limits_.window; }
}
std::string ClientRejoin::status() const {
    switch (stage_) {
    case Stage::closing: return "Closing the interrupted Roblox client";
    case Stage::launching: return attempts_>1 || (attempts_ && !launch_.valid())?"Retrying Roblox launch":"Launching Roblox";
    case Stage::window: return "Waiting for Roblox to open";
    case Stage::focused: return "Waiting for the new Roblox window";
    case Stage::complete: return "Confirming game readiness";
    }
    return {};
}
bool ClientRejoin::retry_launch(Time now) {
    if (attempts_>=limits_.attempts) return false;
    stage_=Stage::launching; candidate_={};
    next_=now+limits_.retry; deadline_=next_+limits_.window;
    return true;
}
std::optional<Window> ClientRejoin::update(Desktop& desktop,Time now) {
    if (now<last_) throw std::runtime_error("Clock moved backwards during client recovery");
    last_=now;
    if (stage_==Stage::complete) return candidate_;
    if (now>=deadline_) {
        // Only resend a completed launch that produced no client. A pending
        // launch or a process still opening its window must not be duplicated.
        bool opening=false;
        if (stage_==Stage::window) for (const auto process:desktop.client_processes())
            if (!excluded_.contains(process)) { opening=true; break; }
        if (stage_==Stage::window && !opening && retry_launch(now)) return {};
        throw std::runtime_error(status()+" timed out");
    }
    if (now<next_) return {};
    next_=now+limits_.poll;
    if (stage_==Stage::closing) {
        if (desktop.client_running(old_.process)) {
            if (!close_sent_) {
                close_sent_=true;
                if (!desktop.close_client(old_)) throw std::runtime_error("Could not close the selected Roblox client");
            }
            return {};
        }
        stage_=Stage::launching; deadline_=now+limits_.window;
    }
    if (stage_==Stage::launching) {
        if (!launch_.valid()) { ++attempts_; launch_=desktop.open_client(target_.uri()); }
        if (!launch_.valid()) throw std::runtime_error("Roblox launch could not be started");
        if (launch_.wait_for(Time{})!=std::future_status::ready) return {};
        if (!launch_.get()) {
            if (retry_launch(now)) return {};
            throw std::runtime_error("Roblox could not be launched after retrying; check its installation");
        }
        stage_=Stage::window;
        return {};
    }
    std::optional<Window> found;
    bool ambiguous=false;
    for (const auto& window:desktop.windows()) {
        if (!window.id || window.process<=0 || !window.bounds.valid() || excluded_.contains(window.process)) continue;
        if (found && found->process!=window.process) return {};
        const double area=window.bounds.width*window.bounds.height;
        const double best=found?found->bounds.width*found->bounds.height:0;
        if (!found || area>best) { found=window; ambiguous=false; }
        else if (window.id!=found->id && area==best) ambiguous=true;
    }
    if (!found || ambiguous) return {};
    if (stage_==Stage::window) {
        candidate_=*found; stage_=Stage::focused; deadline_=now+limits_.focus;
    }
    if (found->process!=candidate_.process) return {};
    const auto observed=desktop.inspect(found->id);
    if (observed.escape_down) throw std::runtime_error("Emergency stop");
    if (!observed.input_allowed) throw std::runtime_error("Input permission lost");
    if (observed.window.id!=found->id || observed.window.process!=found->process || observed.window.bounds!=found->bounds) return {};
    if (!observed.focused) {
        desktop.focus_client(*found);
        return {};
    }
    candidate_=observed.window; stage_=Stage::complete;
    return candidate_;
}
}
