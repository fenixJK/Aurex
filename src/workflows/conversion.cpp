#include "hive/workflows/conversion.hpp"
#include <algorithm>

namespace hive::workflows {
Conversion::Conversion(ConversionConfig c): config_(c) {
    if (c.reading_age<Time(1) || c.observation_timeout<c.reading_age || c.confirmation_timeout<Time(1000)
        || c.total_timeout<c.confirmation_timeout || c.total_timeout<c.observation_timeout
        || c.total_timeout>std::chrono::minutes(30) || c.attempts<1 || c.attempts>5) throw std::invalid_argument("Invalid conversion limits");
}
void Conversion::start(const game::Context& context) {
    if (control_ || !pending_.empty()) throw std::logic_error("Stop the previous conversion first");
    if (!context.window.id || !context.window.bounds.valid() || !context.generation) throw std::invalid_argument("Conversion needs a valid session context");
    started_=context; last_tick_=last_evidence_=context.now;
    minimum_capture_=std::max(Time{},context.now-config_.reading_age);
    confirmed_hive_=false;
    last_attempt_=Time{}; attempts_=0; reason_.clear(); stage_=ConversionStage::waiting;
}
void Conversion::finish(ConversionStage stage,std::string reason,TaskPort& tasks,ModuleScope scope) {
    stage_=stage; reason_=std::move(reason);
    try { tasks.cancel(scope); tasks.results(scope); tasks.release(scope); }
    catch (const std::exception& e) { stage_=ConversionStage::recovery_required; reason_+="; "+std::string(e.what()); }
    control_=false; pending_.clear();
}
void Conversion::stop(TaskPort& tasks,ModuleScope scope) { finish(ConversionStage::stopped,"Conversion stopped",tasks,scope); }
void Conversion::pause(const game::Context& context,TaskPort& tasks,ModuleScope scope) {
    if (stage_!=ConversionStage::waiting && stage_!=ConversionStage::confirming && stage_!=ConversionStage::converting) return;
    if (context.now<last_tick_) throw std::logic_error("Workflow clock moved backwards");
    finish(ConversionStage::paused,"Paused",tasks,scope); paused_at_=context.now;
}
void Conversion::resume(const game::Context& context) {
    if (stage_!=ConversionStage::paused) return;
    if (context.now<paused_at_ || context.generation!=started_.generation || context.window.id!=started_.window.id
        || context.window.process!=started_.window.process || context.window.bounds!=started_.window.bounds) throw std::logic_error("Cannot resume conversion in a changed session");
    const auto duration=context.now-paused_at_;
    started_.now+=duration; last_attempt_+=duration;
    last_tick_=last_evidence_=context.now; minimum_capture_=context.now+Time(1);
    stage_=ConversionStage::waiting; reason_.clear();
}
void Conversion::update(const game::Context& context,const game::State& s,TaskPort& tasks,ModuleScope scope) {
    if (stage_==ConversionStage::idle || stage_==ConversionStage::paused || stage_==ConversionStage::completed || stage_==ConversionStage::recovery_required || stage_==ConversionStage::stopped) return;
    const auto now=context.now;
    auto fail=[&](std::string reason) { finish(ConversionStage::recovery_required,std::move(reason),tasks,scope); };
    if (now<last_tick_ || scope.generation!=context.generation || context.generation!=started_.generation || context.window.id!=started_.window.id || context.window.process!=started_.window.process || context.window.bounds!=started_.window.bounds) { fail("Session or window changed; re-observe before recovery"); return; }
    last_tick_=now;
    const bool converting=s.hive_prompt.has_value(context,config_.reading_age,minimum_capture_)
        && s.hive_prompt.value==game::HivePrompt::stop_making_honey;
    if (converting) started_.now=now;
    if (now-started_.now>=config_.total_timeout) { fail("Honey conversion timed out"); return; }
    if ((s.connection.has_value(context,config_.reading_age) && s.connection.value==game::Connection::disconnected)
        || (s.alive.has_value(context,config_.reading_age) && !s.alive.value)) { fail("Disconnected or dead; recovery required"); return; }
    try {
        for (const auto& result : tasks.results(scope)) if (result.id==pending_) {
            pending_.clear();
            if (result.outcome!=TaskOutcome::executed) { fail("Conversion interaction did not finish; outcome must be checked"); return; }
            minimum_capture_=result.finished+Time(1);
        }
        if (config_.balloon && confirmed_hive_ && pending_.empty()
            && s.hive_prompt.knowledge==Knowledge::absent && s.hive_prompt.usable(context,config_.reading_age,minimum_capture_)
            && s.backpack_percent.has_value(context,config_.reading_age,minimum_capture_) && s.backpack_percent.value==0) {
            finish(ConversionStage::completed,"Backpack empty and conversion prompt gone",tasks,scope);
            return;
        }
        const bool observed=s.at_hive.has_value(context,config_.reading_age,minimum_capture_)
            && s.hive_prompt.has_value(context,config_.reading_age,minimum_capture_)
            && s.hive_prompt.value!=game::HivePrompt::none;
        if (!observed) {
            if (now-last_evidence_>=config_.observation_timeout) fail("Hive conversion prompt could not be read");
            return;
        }
        last_evidence_=now;
        minimum_capture_=std::max(minimum_capture_,s.hive_prompt.captured);
        if (!s.at_hive.value) { fail("Player is no longer confirmed at the hive"); return; }
        confirmed_hive_=true;
        const auto pack=s.backpack_percent.has_value(context,config_.reading_age,minimum_capture_)
            ?std::optional<double>{s.backpack_percent.value}:std::nullopt;
        if (pack && (!std::isfinite(*pack) || *pack<0 || *pack>100)) { fail("Invalid backpack reading"); return; }
        if (!control_) { if (!tasks.acquire(scope)) return; control_=true; }
        if (!pending_.empty()) return;
        const bool empty_prompt=s.hive_prompt.value==game::HivePrompt::collect_pollen;
        if (empty_prompt || (pack==0 && !config_.balloon)) {
            finish(ConversionStage::completed,"Backpack confirmed empty",tasks,scope);
            return;
        }
        if (s.hive_prompt.value==game::HivePrompt::stop_making_honey) { attempts_=0; stage_=ConversionStage::converting; return; }
        if (s.hive_prompt.value!=game::HivePrompt::make_honey) { fail("Invalid hive prompt reading"); return; }
        if (attempts_ && now-last_attempt_<config_.confirmation_timeout) return;
        if (attempts_>=config_.attempts) { fail("Could not confirm conversion after bounded attempts"); return; }
        pending_=tasks.submit(scope,TaskClass::interaction,{{"Start honey conversion",{Key::interact},Time(100),{}}},Time(2000));
        last_attempt_=now; ++attempts_; stage_=ConversionStage::confirming;
    } catch (const std::exception& e) { fail(e.what()); }
}
}
