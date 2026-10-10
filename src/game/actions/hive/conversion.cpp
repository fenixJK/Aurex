#include "conversion.hpp"
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
    started_=context; last_tick_=last_evidence_=last_progress_=context.now;
    minimum_capture_=std::max(Time{},context.now-config_.reading_age);
    confirmed_hive_=false;
    last_backpack_.reset(); backpack_capture_=Time(-1);
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
    last_tick_=last_evidence_=last_progress_=context.now; minimum_capture_=context.now+Time(1);
    confirmed_hive_=false; last_backpack_.reset(); backpack_capture_=Time(-1);
    stage_=ConversionStage::waiting; reason_.clear();
}
bool Conversion::needs_prompt() const {
    if (stage_==ConversionStage::converting)
        return !last_backpack_ || last_tick_-backpack_capture_>config_.reading_age
            || (config_.balloon && *last_backpack_==0) || last_tick_-last_progress_>=config_.confirmation_timeout;
    return stage_==ConversionStage::waiting || stage_==ConversionStage::confirming;
}
void Conversion::update(const game::Context& context,const game::State& s,TaskPort& tasks,ModuleScope scope) {
    if (stage_==ConversionStage::idle || stage_==ConversionStage::paused || stage_==ConversionStage::completed || stage_==ConversionStage::recovery_required || stage_==ConversionStage::stopped) return;
    const auto now=context.now;
    auto fail=[&](std::string reason) { finish(ConversionStage::recovery_required,std::move(reason),tasks,scope); };
    if (now<last_tick_ || scope.generation!=context.generation || context.generation!=started_.generation || context.window.id!=started_.window.id || context.window.process!=started_.window.process || context.window.bounds!=started_.window.bounds) { fail("Session or window changed; re-observe before recovery"); return; }
    last_tick_=now;
    if ((s.connection.has_value(context,config_.reading_age) && s.connection.value==game::Connection::disconnected)
        || (s.alive.has_value(context,config_.reading_age) && !s.alive.value)) { fail("Disconnected or dead; recovery required"); return; }
    try {
        for (const auto& result : tasks.results(scope)) if (result.id==pending_) {
            pending_.clear();
            if (result.outcome!=TaskOutcome::executed) { fail("Conversion interaction did not finish; outcome must be checked"); return; }
            minimum_capture_=result.finished+Time(1);
        }
        if (s.at_hive.has_value(context,config_.reading_age,minimum_capture_)) {
            if (!s.at_hive.value) { fail("Player is no longer confirmed at the hive"); return; }
            confirmed_hive_=true;
        }
        const bool observed=confirmed_hive_ && s.hive_prompt.has_value(context,config_.reading_age,minimum_capture_)
            && s.hive_prompt.value!=game::HivePrompt::none;
        if (observed) {
            last_evidence_=now;
            minimum_capture_=std::max(minimum_capture_,s.hive_prompt.captured);
            if (s.hive_prompt.value==game::HivePrompt::stop_making_honey) {
                if (stage_!=ConversionStage::converting) last_progress_=now;
                attempts_=0; stage_=ConversionStage::converting; started_.now=now;
            }
        }
        const auto pack=s.backpack_percent.has_value(context,config_.reading_age,std::max(minimum_capture_,backpack_capture_))
            ?std::optional<double>{s.backpack_percent.value}:std::nullopt;
        if (pack && (!std::isfinite(*pack) || *pack<0 || *pack>100)) { fail("Invalid backpack reading"); return; }
        if (confirmed_hive_ && pack) {
            if (s.backpack_percent.captured>backpack_capture_) {
                if ((stage_==ConversionStage::converting || (stage_==ConversionStage::confirming && pending_.empty()))
                    && last_backpack_ && *pack<*last_backpack_) {
                    stage_=ConversionStage::converting; attempts_=0;
                    last_progress_=started_.now=now;
                }
                last_backpack_=pack; backpack_capture_=s.backpack_percent.captured;
            }
            if (stage_==ConversionStage::converting && (!config_.balloon || *pack>0)) last_evidence_=now;
        }
        if (confirmed_hive_ && pending_.empty() && ((observed && s.hive_prompt.value==game::HivePrompt::collect_pollen)
            || (pack==0 && !config_.balloon))) {
            finish(ConversionStage::completed,"Backpack confirmed empty",tasks,scope);
            return;
        }
        if (config_.balloon && confirmed_hive_ && pending_.empty()
            && s.hive_prompt.knowledge==Knowledge::absent && s.hive_prompt.usable(context,config_.reading_age,minimum_capture_)
            && pack==0) {
            finish(ConversionStage::completed,"Backpack empty and conversion prompt gone",tasks,scope);
            return;
        }
        if (now-started_.now>=config_.total_timeout) { fail("Honey conversion timed out"); return; }
        if (!observed) {
            if (now-last_evidence_>=config_.observation_timeout) fail("Hive conversion prompt could not be read");
            return;
        }
        if (!pending_.empty()) return;
        if (s.hive_prompt.value==game::HivePrompt::stop_making_honey) return;
        if (s.hive_prompt.value!=game::HivePrompt::make_honey) { fail("Invalid hive prompt reading"); return; }
        if (!needs_prompt()) return;
        if (attempts_ && now-last_attempt_<config_.confirmation_timeout) return;
        if (attempts_>=config_.attempts) { fail("Could not confirm conversion after bounded attempts"); return; }
        if (!control_) { if (!tasks.acquire(scope)) return; control_=true; }
        pending_=tasks.submit(scope,TaskClass::interaction,{{"Start honey conversion",{Key::interact},Time(100),{}}},Time(2000));
        last_attempt_=now; ++attempts_; stage_=ConversionStage::confirming;
    } catch (const std::exception& e) { fail(e.what()); }
}
}
