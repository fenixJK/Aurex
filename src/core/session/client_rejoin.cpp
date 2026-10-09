#include "client_rejoin.hpp"

namespace hive {
ClientRejoin::ClientRejoin(game::JoinTarget target,Window old,const std::vector<std::int64_t>& existing,Time now,RejoinLimits limits)
    :target_(std::move(target)),limits_(limits),old_(std::move(old)),deadline_(now+limits.close),next_(now),last_(now) {
    if ((old_.id && old_.process<=0) || (!old_.id && old_.process) || limits.close<=Time{} || limits.window<=Time{} || limits.focus<=Time{} || limits.poll<=Time{})
        throw std::invalid_argument("Invalid client recovery configuration");
    if(old_.id) {
        for (const auto process:existing) excluded_.insert(process);
        excluded_.insert(old_.process);
    } else stage_=Stage::launching;
}
std::string ClientRejoin::status() const {
    switch (stage_) {
    case Stage::closing: return "Closing the interrupted Roblox client";
    case Stage::launching: return "Launching Roblox";
    case Stage::window: return "Waiting for Roblox to open";
    case Stage::focused: return "Waiting for the new Roblox window";
    case Stage::complete: return "Confirming game readiness";
    }
    return {};
}
std::optional<Window> ClientRejoin::update(Desktop& desktop,Time now) {
    if (now<last_) throw std::runtime_error("Clock moved backwards during client recovery");
    last_=now;
    if (stage_==Stage::complete) return candidate_;
    if (now>=deadline_) throw std::runtime_error(status()+" timed out");
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
        stage_=Stage::launching;
    }
    if (stage_==Stage::launching) {
        if (!desktop.open_client(target_.uri())) throw std::runtime_error("Roblox could not be launched; check its installation");
        stage_=Stage::window; deadline_=now+limits_.window;
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
