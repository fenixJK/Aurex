#include "hive/modules/tools/rolling.hpp"
#include "hive/game/detection/dialog.hpp"
#include "hive/game/detection/menus.hpp"

namespace hive::features::tools {
namespace {
using namespace automation;
class Rolling : public Program {
protected:
    std::shared_ptr<RollProgress> progress_;
    RollReport report_;
    Time delay_,minimum_{},deadline_{},frame_{Time(-1)};
    bool acted_{};
    Time settle_{};
    Step waiting(const View& view,const char* message) {
        if (view.context.now>=deadline_) { report_.status="Could not read the result; no further roll submitted"; publish(); return Done{report_.status}; }
        return Wait{Time(100),message};
    }
    bool fresh(const View& view) {
        const auto& c=view.context;
        if (!deadline_.count()) { minimum_=c.now; deadline_=c.now+Time(8000); }
        if (c.now<minimum_ || !view.observations || !view.observations->text.has_value(c,Time(2000),minimum_)) return false;
        if (view.observations->text.captured<=frame_) return false;
        frame_=view.observations->text.captured; return true;
    }
    void publish() { progress_->set(report_); }
    Step finish(std::string text,bool match=false) { report_.matched=match; report_.status=std::move(text); publish(); return Done{report_.status}; }
    Step click(const View& view,Point point,std::string message,Time settle) {
        acted_=true; settle_=settle; report_.status=message; publish();
        const auto& text=view.observations->text;
        return UseControl{{Knowledge::present,point,text.captured,text.window,text.generation},0,std::move(message),game::pointer_rest(view.context,*view.observations)};
    }
public:
    Rolling(Time delay,std::shared_ptr<RollProgress> progress):progress_(std::move(progress)),delay_(delay) {
        if (!progress_ || delay<Time(500) || delay>Time(5000)) throw std::invalid_argument("Roll delay must be between 0.5 and 5 seconds");
        report_.status="Waiting for the open result"; publish();
    }
    RunConfig config() const override { return {std::chrono::hours(8),28,{},true,true}; }
    std::vector<Rect> text_regions() const override { return {{.1,0,.8,.95}}; }
    void step_completed(const View& view) override {
        if (!acted_) return;
        acted_=false; minimum_=view.context.now+settle_; deadline_=minimum_+Time(8000);
    }
    void finished(State state) override {
        if (state==State::stopped || state==State::failed) { report_.status=state==State::stopped?"Stopped":"Stopped after an error"; publish(); }
    }
};
class Ssa final : public Rolling {
    SsaPlan plan_;
    enum class Stage { current,purchase,result } stage_{Stage::current};
    bool pressed_{};
public:
    Ssa(SsaPlan plan,std::shared_ptr<RollProgress> progress):Rolling(plan.delay,std::move(progress)),plan_(std::move(plan)) {
        validate(plan_.filter);
        if (!plan_.honey_billions || plan_.honey_billions>1000000) throw std::invalid_argument("Set a finite honey limit in billions");
    }
    Step next(const View& view) override {
        if (!fresh(view)) return waiting(view,"Reading SSA");
        const auto& c=view.context;
        const auto& state=*view.observations;
        const auto purchase=game::read_dialog(c,state.text,{{"guarantee 2 passive abilities"},{.15,.15,.7,.7},.25f,game::PromptMatch::phrase});
        const auto result=read_amulet(c,state);
        if (!purchase.has_value(c,Time(2000),minimum_) && stage_!=Stage::purchase && result.has_value(c,Time(2000),minimum_)) {
            report_.result.clear(); for (const auto& name:result.value.passives) { if (!report_.result.empty()) report_.result+=" / "; report_.result+=name; }
            if (matches(result.value,plan_.filter)) return finish("Match found; choose Keep Old or Replace in-game",true);
            stage_=Stage::purchase; pressed_=false;
        }
        const unsigned cost=plan_.filter.double_passive?500:10;
        if (stage_==Stage::purchase || stage_==Stage::current) {
            if (report_.attempts>=plan_.honey_billions/cost) return finish("Honey limit reached");
            if (purchase.has_value(c,Time(2000),minimum_)) {
                ++report_.attempts; stage_=Stage::result; pressed_=false;
                return click(view,plan_.filter.double_passive?purchase.value.yes:purchase.value.no,"Roll SSA",delay_);
            }
            if (!pressed_ && (result.has_value(c,Time(2000),minimum_) || game::read_prompt_landmark(c,state.text,
                {{"Generate a Supreme Star Amulet"},{.15,0,.7,.3},.5f,game::PromptMatch::phrase}).has_value(c,Time(2000),minimum_))) {
                stage_=Stage::purchase; pressed_=true; acted_=true; settle_=Time(100);
                return Hold{{Key::interact},Time(80)};
            }
        }
        return waiting(view,"Waiting for the SSA result");
    }
};
class Jelly final : public Rolling {
    JellyPlan plan_;
public:
    Jelly(JellyPlan plan,std::shared_ptr<RollProgress> progress):Rolling(plan.delay,std::move(progress)),plan_(std::move(plan)) {
        validate(plan_.filter,plan_.require_mutation);
        if (plan_.require_mutation) validate(plan_.mutations);
        if (!plan_.rolls || plan_.rolls>1000000) throw std::invalid_argument("Set a finite Royal Jelly roll limit");
    }
    Step next(const View& view) override {
        if (!fresh(view)) return waiting(view,"Reading bee result");
        const auto result=read_jelly(view.context,*view.observations);
        if (!result.has_value(view.context,Time(2000),minimum_)) return waiting(view,"Waiting for the bee result");
        const auto& bee=result.value.bee;
        report_.result=(bee.gifted?"Gifted ":"")+bee.name+" Bee";
        if ((bee.gifted && plan_.filter.stop_gifted) || (bee.mythic && plan_.filter.stop_mythic)) return finish("Match found; bee result left open",true);
        if (plan_.filter.names.contains(bee.name) || (plan_.require_mutation && plan_.filter.names.empty())) {
            if (!plan_.require_mutation) return finish("Match found; bee result left open",true);
            const auto mutation=read_mutation(view.context,*view.observations);
            if (!mutation.usable(view.context,Time(2000),minimum_)) return waiting(view,"Reading mutation");
            if (mutation.knowledge==Knowledge::present && matches(mutation.value.mutation,plan_.mutations)) {
                report_.result+=" · "+describe(mutation.value.mutation); return finish("Match found; bee result left open",true);
            }
        }
        if (report_.attempts>=plan_.rolls) return finish("Royal Jelly roll limit reached");
        if (!result.value.again) return waiting(view,"Waiting for the Royal Jelly button");
        ++report_.attempts;
        return click(view,*result.value.again,"Roll Royal Jelly",delay_);
    }
};
}
std::unique_ptr<automation::Program> roll_ssa(SsaPlan plan,std::shared_ptr<RollProgress> progress) { return std::make_unique<Ssa>(std::move(plan),std::move(progress)); }
std::unique_ptr<automation::Program> roll_jelly(JellyPlan plan,std::shared_ptr<RollProgress> progress) { return std::make_unique<Jelly>(std::move(plan),std::move(progress)); }
}
