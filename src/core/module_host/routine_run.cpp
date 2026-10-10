#include "routine_run.hpp"
#include <algorithm>
#include <set>

namespace hive::extensions {
namespace {
std::vector<RoutineEntry> ordered(std::vector<RoutineEntry> entries,const std::vector<std::string>& order) {
    int rank=99;
    std::set<std::string> seen;
    for (const auto& id:order) {
        if (!seen.insert(id).second) continue;
        const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& entry) {
            return entry.id==id;
        });
        if (found!=entries.end()) found->participation.priority=rank--;
    }
    return entries;
}
}
RoutineRun::RoutineRun(Host& host,Session& session,std::unique_ptr<automation::ActivityPolicy> policy,std::optional<automation::Launch> preparation,std::vector<std::string> order)
    :host_(host),session_(session),schedule_(ordered(host.routine_entries(),order),std::move(policy)),preparation_(std::move(preparation)) {}
RoutineRun::~RoutineRun() {
    if (state_==RoutineState::running || state_==RoutineState::pausing || state_==RoutineState::paused)
        stop("Routine closed");
}
SessionView RoutineRun::view() {
    auto snapshot=session_.snapshot();
    return {std::move(snapshot.status),snapshot.now,snapshot.generation,std::move(snapshot.feature),snapshot.pause_requests,{},snapshot.session_id,std::move(snapshot.server)};
}
void RoutineRun::start(Window window) {
    if (state_!=RoutineState::ready) throw std::logic_error("Create a new routine before starting again");
    session_.start_managed(std::move(window));
    const auto snapshot=session_.snapshot();
    session_id_=snapshot.session_id; pause_requests_=snapshot.pause_requests; state_=RoutineState::running;
    host_.update(view());
    if (preparation_) {
        try { session_.run_feature(std::move(*preparation_)); preparing_=true; preparation_.reset(); }
        catch (...) { fail("Session preparation could not start"); throw; }
    }
}
void RoutineRun::fail(std::string reason) {
    reason_=std::move(reason); state_=RoutineState::failed;
    if (session_id_ && session_.snapshot().session_id==session_id_) session_.stop(reason_);
}
SessionView RoutineRun::tick() {
    auto current=view();
    if (state_==RoutineState::ready || state_==RoutineState::stopped || state_==RoutineState::failed) return current;
    try {
        if (current.session_id!=session_id_) throw std::runtime_error("Routine session changed");
        const auto requests=current.pause_requests;
        if (requests!=pause_requests_) {
            pause_requests_=requests;
            if (state_==RoutineState::paused) { resume(); current=view(); if (state_==RoutineState::failed) return current; }
            else if (state_==RoutineState::pausing) cancel_pause();
            else request_pause();
        }
        if (state_==RoutineState::paused && current.status.state==RunState::running)
            throw std::runtime_error("Routine resumed outside its controller");
        if (current.status.state!=RunState::running && !(state_==RoutineState::paused && current.status.state==RunState::paused)) {
            if (current.status.state==RunState::paused) { session_.stop(current.status.reason); current=view(); }
            state_=RoutineState::stopped; reason_=current.status.reason;
            host_.update(current); return current;
        }
        host_.update(current);
        if (preparing_) {
            if (!current.feature) throw std::runtime_error("Session preparation lost its result");
            if (current.feature->state==automation::State::running) return current;
            if (current.feature->state!=automation::State::completed)
                throw std::runtime_error(current.feature->message.empty()?"Session preparation did not finish":current.feature->message);
            preparing_=false;
        }
        if (current.feature && current.feature->state==automation::State::running) {
            const auto module=host_.entries().find(current.feature->module_id);
            if (module!=host_.entries().end() && !module->second.active())
                session_.stop_feature(module->first,module->second.issue);
        }
        current=view();
        if (current.session_id!=session_id_) throw std::runtime_error("Routine session changed");
        if (state_==RoutineState::paused) return current;
        if (current.status.state!=RunState::running) {
            if (current.status.state==RunState::paused) { session_.stop(current.status.reason); current=view(); }
            state_=RoutineState::stopped; reason_=current.status.reason; return current;
        }
        if (state_==RoutineState::running && schedule_.should_yield(host_,current)) session_.yield_feature(current.feature->module_id);
        if (auto launch=schedule_.next(host_,current,state_!=RoutineState::pausing)) {
            const auto id=launch->module_id;
            try { session_.run_feature(std::move(*launch)); }
            catch (const std::exception& e) {
                if (session_.status().state!=RunState::running) throw;
                schedule_.launch_failed(); host_.fail_module(id,e.what());
            } catch (...) {
                if (session_.status().state!=RunState::running) throw;
                schedule_.launch_failed(); host_.fail_module(id,"Module could not start");
            }
        }
        else if (state_==RoutineState::pausing && (!current.feature || automation::can_handoff(*current.feature))) {
            session_.pause_between_features(); state_=RoutineState::paused;
            host_.update(view());
        }
    } catch (const std::exception& e) { fail(e.what()); }
    catch (...) { fail("Unexpected routine failure"); }
    return view();
}
void RoutineRun::request_pause() {
    if (state_==RoutineState::running) state_=RoutineState::pausing;
}
void RoutineRun::cancel_pause() {
    if (state_==RoutineState::pausing) state_=RoutineState::running;
}
void RoutineRun::resume() {
    if (state_!=RoutineState::paused) throw std::logic_error("Routine is not paused");
    if (session_.snapshot().session_id!=session_id_) { fail("Routine session changed"); return; }
    try {
        session_.resume();
        const auto current=view();
        if (current.status.state!=RunState::running) throw std::runtime_error(current.status.reason.empty()?"Could not resume routine":current.status.reason);
        state_=RoutineState::running;
    } catch (const std::exception& e) { fail(e.what()); }
    catch (...) { fail("Could not resume routine"); }
}
void RoutineRun::stop(std::string reason) {
    reason_=std::move(reason); state_=RoutineState::stopped;
    if (session_id_ && session_.snapshot().session_id==session_id_) session_.stop(reason_);
}
}
