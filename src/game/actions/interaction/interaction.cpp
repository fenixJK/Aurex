#include "interaction.hpp"
#include <algorithm>

namespace hive::workflows {
Interaction::Interaction(game::TextTarget prompt,game::TextTarget result,Time timeout)
    :prompt_(std::move(prompt)),result_(std::move(result)),timeout_(timeout) {
    std::visit([](const auto& target) { game::validate(target); },prompt_);
    std::visit([](const auto& target) { game::validate(target); },result_);
    if (timeout<Time(1000) || timeout>Time(60000)) throw std::invalid_argument("Invalid interaction timeout");
}
Interaction::Interaction(game::PromptLandmark prompt,game::PromptLandmark result,Time timeout)
    :prompt_(std::move(prompt)),result_(std::move(result)),timeout_(timeout) {
    std::visit([](const auto& target) { game::validate(target); },prompt_);
    std::visit([](const auto& target) { game::validate(target); },result_);
    if (timeout<Time(1000) || timeout>Time(60000)) throw std::invalid_argument("Invalid interaction timeout");
}
game::Reading<bool> Interaction::read(const Target& target,const ObservationContext& c,const game::State& state) const {
    if (const auto* text=std::get_if<game::TextTarget>(&target)) return game::read_text_target(c,state,*text,minimum_);
    return game::read_prompt_landmark(c,state.text,std::get<game::PromptLandmark>(target));
}
void Interaction::start(const ObservationContext& context) {
    if (stage_!=InteractionStage::idle || !context.window.id || !context.window.bounds.valid() || !context.generation) throw std::logic_error("Interaction needs a new valid session");
    started_=context; minimum_=last_tick_=context.now; stage_=InteractionStage::waiting;
    if (const auto* station=std::get_if<game::TextTarget>(&prompt_); station && station->station_name)
        minimum_=std::max(Time{},context.now-Time(2000));
}
void Interaction::finish(InteractionStage stage,std::string reason,TaskPort& tasks,ModuleScope scope) {
    stage_=stage; reason_=std::move(reason);
    try { tasks.cancel(scope); tasks.results(scope); tasks.release(scope); }
    catch (const std::exception& e) { stage_=InteractionStage::failed; reason_+="; "+std::string(e.what()); }
    pending_.clear();
}
void Interaction::stop(TaskPort& tasks,ModuleScope scope) { finish(InteractionStage::stopped,"Interaction stopped",tasks,scope); }
void Interaction::update(const ObservationContext& c,const game::State& state,TaskPort& tasks,ModuleScope scope) {
    if (stage_==InteractionStage::idle || stage_==InteractionStage::completed || stage_==InteractionStage::failed || stage_==InteractionStage::stopped) return;
    auto fail=[&](const std::string& reason) { finish(InteractionStage::failed,reason,tasks,scope); };
    if (c.now<last_tick_ || c.generation!=started_.generation || scope.generation!=c.generation
        || c.window.id!=started_.window.id || c.window.process!=started_.window.process || c.window.bounds!=started_.window.bounds) { fail("Interaction context changed"); return; }
    last_tick_=c.now;
    if (c.now-started_.now>=timeout_) {
        const auto* prompt=std::get_if<game::PromptLandmark>(&prompt_);
        fail((prompt?prompt->phrases.front():"Station")+std::string(" result was not confirmed")); return;
    }
    if ((state.connection.has_value(c,Time(2000)) && state.connection.value==game::Connection::disconnected)
        || (state.alive.has_value(c,Time(2000)) && !state.alive.value)) { fail("Game disconnected or player respawning"); return; }
    try {
        for (const auto& result : tasks.results(scope)) if (result.id==pending_) {
            pending_.clear();
            if (result.outcome!=TaskOutcome::executed) { fail("Interaction interrupted; check the game before retrying"); return; }
            pressed_=result.finished; minimum_=pressed_+Time(1); stage_=InteractionStage::confirming;
        }
        const bool exact=std::holds_alternative<game::PromptLandmark>(prompt_);
        if (exact && !owned_) {
            if (!tasks.acquire(scope)) return;
            owned_=true; minimum_=c.now+Time(1);
        }
        if (!state.text.has_value(c,Time(2000),minimum_)) return;
        if (stage_==InteractionStage::waiting) {
            if (state.text.captured<last_result_) return;
            if (state.text.captured>last_result_) {
                last_result_=state.text.captured;
                const auto result=read(result_,c,state);
                if (result.knowledge==Knowledge::present && result.value) {
                    if (exact) fail("Interaction already shows its result");
                    else finish(InteractionStage::completed,"Station result confirmed",tasks,scope);
                    return;
                }
                const auto prompt=read(prompt_,c,state);
                prompt_seen_=(result.knowledge==Knowledge::absent || (exact && !result.value))
                    && prompt.knowledge==Knowledge::present && prompt.value;
            }
            if (!prompt_seen_ || !tasks.acquire(scope)) return;
            pending_=tasks.submit(scope,TaskClass::interaction,{{"Use station",{Key::interact},Time(100),{}}},Time(2000));
            ++attempts_; stage_=InteractionStage::pressing;
        } else if (stage_==InteractionStage::confirming && state.text.captured>last_result_) {
            const auto observed=read(result_,c,state);
            const bool result=observed.knowledge==Knowledge::present && observed.value
                && (!exact || !read(prompt_,c,state).value);
            last_result_=state.text.captured;
            if (result) finish(InteractionStage::completed,"Station result confirmed",tasks,scope);
            else if (!exact && attempts_<3 && observed.knowledge==Knowledge::absent
                && state.text.captured>=pressed_+Time(750) && read(prompt_,c,state).value) {
                prompt_seen_=true; stage_=InteractionStage::waiting;
            }
        }
    } catch (const std::exception& e) { fail(e.what()); }
}
}
