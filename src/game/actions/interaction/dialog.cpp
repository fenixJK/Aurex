#include "dialog.hpp"
#include "game/detection/menus.hpp"

namespace hive::workflows {
void validate(const DialogPlan& plan) {
    game::validate(plan.question);
    if ((plan.choice!=DialogChoice::yes && plan.choice!=DialogChoice::no) || plan.timeout<Time(1000) || plan.timeout>Time(30000))
        throw std::invalid_argument("Invalid dialog action");
}
Dialog::Dialog(DialogPlan plan):plan_(std::move(plan)) { validate(plan_); }
void Dialog::start(const ObservationContext& c) {
    if (stage_!=DialogStage::idle || !c.window.id || !c.window.bounds.valid() || !c.generation || c.now<Time::zero() || c.now>Time::max()-plan_.timeout)
        throw std::logic_error("Dialog needs a new valid session");
    started_=c; minimum_=last_=c.now; stage_=DialogStage::reading; reason_="Reading confirmation dialog";
}
void Dialog::finish(DialogStage stage,std::string reason,TaskPort& tasks,ModuleScope scope) {
    stage_=stage; reason_=std::move(reason);
    try { tasks.cancel(scope); tasks.results(scope); tasks.release(scope); }
    catch (const std::exception& e) { timed_out_=false; stage_=DialogStage::failed; reason_+="; "+std::string(e.what()); }
    pending_.clear();
}
void Dialog::update(const ObservationContext& c,const game::State& state,TaskPort& tasks,ModuleScope scope) {
    if (stage_!=DialogStage::reading && stage_!=DialogStage::clicking && stage_!=DialogStage::closing && stage_!=DialogStage::parking) return;
    auto fail=[&](std::string reason) { finish(DialogStage::failed,std::move(reason),tasks,scope); };
    if (c.now<last_ || c.generation!=started_.generation || scope.generation!=c.generation || c.window.id!=started_.window.id
        || c.window.process!=started_.window.process || c.window.bounds!=started_.window.bounds) { fail("Dialog context changed"); return; }
    last_=c.now;
    if (stage_!=DialogStage::parking && c.now-started_.now>=plan_.timeout) { timed_out_=true; fail(stage_==DialogStage::reading?"Confirmation dialog could not be read":"Confirmation dialog did not close after clicking"); return; }
    if ((state.connection.has_value(c,Time(2000)) && state.connection.value==game::Connection::disconnected)
        || (state.alive.has_value(c,Time(2000)) && !state.alive.value)) { fail("Game disconnected or player respawning"); return; }
    try {
        if (!owned_) {
            if (!tasks.acquire(scope)) return;
            owned_=true; minimum_=c.now+Time(1);
        }
        for (const auto& result : tasks.results(scope)) if (result.id==pending_) {
            pending_.clear();
            if (result.outcome!=TaskOutcome::executed) { fail("Dialog click was interrupted"); return; }
            if (result.finished<started_.now || result.finished>c.now) { fail("Invalid dialog completion time"); return; }
            if (stage_==DialogStage::parking) { finish(DialogStage::completed,"Dialog closed",tasks,scope); return; }
            minimum_=result.finished+Time(1); stage_=DialogStage::closing; reason_="Waiting for dialog to close";
        }
        if (stage_==DialogStage::clicking || stage_==DialogStage::parking) return;
        const auto reading=plan_.read_question?game::read_dialog(c,state.text,plan_.question):state.confirmation;
        if (!reading.usable(c,Time(2000),minimum_)) return;
        if (reading.captured<=frame_) return;
        frame_=reading.captured;
        if (stage_==DialogStage::closing) {
            if (reading.knowledge==Knowledge::absent) {
                pending_=tasks.submit(scope,TaskClass::interaction,
                    {{"Move away from dialog",{},Time(40),game::pointer_rest(c,state)}},Time(1000));
                stage_=DialogStage::parking;
            }
            return;
        }
        if (reading.knowledge!=Knowledge::present) return;
        const auto point=plan_.choice==DialogChoice::yes?reading.value.yes:reading.value.no;
        pending_=tasks.submit(scope,TaskClass::interaction,{
            {"Move to dialog choice",{},Time(40),point},
            {"Choose dialog response",{Key::primary},Time(60),{}}},Time(1000));
        stage_=DialogStage::clicking; reason_="Answering confirmation dialog";
    } catch (const std::exception& e) { fail(e.what()); }
}
}
