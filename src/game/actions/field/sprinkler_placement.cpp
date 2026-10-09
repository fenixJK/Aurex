#include "sprinkler_placement.hpp"
#include <algorithm>

namespace hive::workflows {
void validate(const SprinklerPlan& plan) {
    auto name=[](const std::string& value) { return !value.empty() && value.size()<=80
        && std::none_of(value.begin(),value.end(),[](unsigned char c) { return c<32 || c==127; }); };
    if (!name(plan.field) || !name(plan.site) || plan.slot<1 || plan.slot>7
        || plan.jump<Time::zero() || (plan.jump>Time::zero() && plan.jump<Time(50)) || plan.jump>Time(500) || plan.timeout<Time(1000) || plan.timeout>Time(30000))
        throw std::invalid_argument("Invalid sprinkler placement plan");
    if (plan.completion!=SprinklerCompletion::observed && plan.completion!=SprinklerCompletion::attempted)
        throw std::invalid_argument("Invalid sprinkler completion policy");
}
Sprinkler::Sprinkler(SprinklerPlan plan):plan_(std::move(plan)) { validate(plan_); }
void Sprinkler::start(const ObservationContext& context) {
    if (stage_!=SprinklerStage::idle || !context.generation || !context.window.id || !context.window.bounds.valid()
        || context.now<Time::zero() || context.now>Time::max()-plan_.timeout)
        throw std::invalid_argument("Sprinkler placement needs a new valid session");
    started_=context; minimum_=last_tick_=context.now; stage_=SprinklerStage::waiting;
    reason_=plan_.completion==SprinklerCompletion::observed?"Confirming sprinkler site and hotbar":"Checking sprinkler feedback";
}
void Sprinkler::finish(SprinklerStage stage,std::string reason,TaskPort& tasks,ModuleScope scope) {
    stage_=stage; reason_=std::move(reason);
    try { tasks.cancel(scope); tasks.results(scope); tasks.release(scope); }
    catch (const std::exception& e) { stage_=SprinklerStage::failed; reason_+="; "+std::string(e.what()); }
    pending_.clear();
}
void Sprinkler::stop(TaskPort& tasks,ModuleScope scope) { finish(SprinklerStage::stopped,"Sprinkler placement stopped",tasks,scope); }
void Sprinkler::update(const ObservationContext& c,const game::State& state,const Observation<game::SprinklerSite>& reading,TaskPort& tasks,ModuleScope scope) {
    if (stage_==SprinklerStage::idle || stage_==SprinklerStage::completed || stage_==SprinklerStage::failed || stage_==SprinklerStage::stopped) return;
    auto fail=[&](const std::string& reason) { finish(SprinklerStage::failed,reason,tasks,scope); };
    if (c.now<last_tick_ || c.generation!=started_.generation || scope.generation!=c.generation || c.window.id!=started_.window.id
        || c.window.process!=started_.window.process || c.window.bounds!=started_.window.bounds) { fail("Sprinkler placement context changed"); return; }
    last_tick_=c.now;
    if (c.now-started_.now>=plan_.timeout) {
        fail(plan_.completion==SprinklerCompletion::attempted?"Sprinkler feedback timed out; input stopped":
            attempted_?"Sprinkler result unconfirmed; placement was not repeated":"Sprinkler prerequisites were not confirmed"); return;
    }
    if ((state.connection.has_value(c,Time(2000),started_.now) && state.connection.value==game::Connection::disconnected)
        || (state.alive.has_value(c,Time(2000),started_.now) && !state.alive.value)) { fail("Game disconnected or player respawning"); return; }
    try {
        for (const auto& result : tasks.results(scope)) if (result.id==pending_) {
            pending_.clear();
            if (result.outcome!=TaskOutcome::executed) { fail("Sprinkler input interrupted; placement needs a fresh check"); return; }
            input_finished_=result.finished; minimum_=result.finished+Time(1); last_frame_=Time(-1);
            stage_=SprinklerStage::confirming;
            reason_=plan_.completion==SprinklerCompletion::observed?"Confirming sprinkler placement":"Checking sprinkler feedback";
        }
        if (stage_==SprinklerStage::placing) return;
        if (plan_.completion==SprinklerCompletion::attempted) { update_attempt(c,state,tasks,scope); return; }
        if (!reading.has_value(c,Time(2000),minimum_)) return;
        if (reading.captured<last_frame_) return;
        const auto& site=reading.value;
        if (site.field!=plan_.field || site.site!=plan_.site) { fail("Sprinkler location does not match the requested site"); return; }
        if (site.builder_slot!=plan_.slot) { fail("Sprinkler Builder is not confirmed in the configured hotbar slot"); return; }
        if (site.feedback!=game::SprinklerFeedback::none && site.feedback!=game::SprinklerFeedback::too_low
            && site.feedback!=game::SprinklerFeedback::too_high && site.feedback!=game::SprinklerFeedback::unavailable) {
            fail("Invalid sprinkler feedback"); return;
        }
        if (!site.aligned) return;
        if (stage_==SprinklerStage::confirming && site.feedback!=game::SprinklerFeedback::none) {
            fail("Game rejected sprinkler placement; check position and jump timing"); return;
        }
        last_frame_=reading.captured;
        if (site.placed) { finish(SprinklerStage::completed,attempted_?"Sprinkler placement confirmed":"Sprinkler already confirmed at this site",tasks,scope); return; }
        if (stage_!=SprinklerStage::waiting || !site.ready || !tasks.acquire(scope)) return;
        dispatch(tasks,scope);
    } catch (const std::exception& e) { fail(e.what()); }
}
void Sprinkler::dispatch(TaskPort& tasks,ModuleScope scope) {
    std::vector<Action> actions;
    if (plan_.jump>Time::zero()) {
        actions.push_back({"Jump for sprinkler",{Key::jump},Time(50),{}});
        auto delay=plan_.jump-Time(50);
        while (delay>Time::zero()) {
            const auto part=std::min(delay,Time(250)); actions.push_back({"Reach placement height",{},part,{}}); delay-=part;
        }
    }
    actions.push_back({"Place sprinkler",{static_cast<Key>(static_cast<int>(Key::hotbar_1)+plan_.slot-1)},Time(50),{}});
    pending_=tasks.submit(scope,TaskClass::interaction,std::move(actions),Time(1000)); attempted_=true; ++attempts_;
    stage_=SprinklerStage::placing; reason_="Placing sprinkler";
}
void Sprinkler::update_attempt(const ObservationContext& c,const game::State& state,TaskPort& tasks,ModuleScope scope) {
    const auto reading=game::read_sprinkler_feedback(c,state,minimum_);
    if (!attempted_) {
        if (reading.has_value(c,Time(2000),minimum_) && reading.value!=game::SprinklerFeedback::none) return;
        if (tasks.acquire(scope)) dispatch(tasks,scope);
        return;
    }
    if (c.now-input_finished_<Time(500)) return;
    if (!reading.has_value(c,Time(2000),minimum_)) return;
    if (reading.captured-input_finished_<Time(500)) return;
    if (reading.captured<=last_frame_) return;
    last_frame_=reading.captured;
    if (reading.value!=game::SprinklerFeedback::none) {
        if (stage_!=SprinklerStage::confirming) return;
        const auto adjusted=reading.value==game::SprinklerFeedback::too_high?std::max(Time(50),plan_.jump-Time(50)):
            reading.value==game::SprinklerFeedback::too_low?std::min(Time(500),plan_.jump+Time(50)):plan_.jump;
        if (plan_.jump==Time::zero() || attempts_>=3 || adjusted==plan_.jump) {
            finish(SprinklerStage::failed,"Game rejected sprinkler placement",tasks,scope); return;
        }
        plan_.jump=adjusted; stage_=SprinklerStage::waiting;
        reason_="Waiting for sprinkler warning to clear before retry";
        return;
    }
    if (stage_==SprinklerStage::confirming) {
        finish(SprinklerStage::completed,"Sprinkler input sent; placement not visually confirmed",tasks,scope);
        return;
    }
    if (tasks.acquire(scope)) dispatch(tasks,scope);
}

}
