#include "rolling.hpp"
#include "game/detection/dialog.hpp"
#include "game/detection/menus.hpp"
#include "game/actions/inventory/feeding.hpp"

namespace hive::features::tools {
namespace {
using namespace automation;
class Rolling : public Program {
protected:
    std::shared_ptr<RollProgress> progress_;
    RollReport report_;
    Time minimum_{},deadline_{},frame_{Time(-1)};
    bool acted_{};
    Step waiting(const View& view,const char* message) {
        if (view.context.now>=deadline_) { report_.status="Could not read the result; no further roll submitted"; publish(); return Done{report_.status}; }
        return Wait{Time(100),message};
    }
    bool fresh(const View& view,bool confirmation=false) {
        const auto& c=view.context;
        if (!deadline_.count()) { minimum_=c.now; deadline_=c.now+Time(8000); }
        if (c.now<minimum_ || !view.observations) return false;
        const auto& state=*view.observations;
        const auto captured=confirmation?state.confirmation.captured:state.text.captured;
        if (!(confirmation?state.confirmation.has_value(c,Time(2000),minimum_):state.text.has_value(c,Time(2000),minimum_)) || captured<=frame_) return false;
        frame_=captured; return true;
    }
    void publish() { progress_->set(report_); }
    Step finish(std::string text,bool match=false) { report_.matched=match; report_.status=std::move(text); publish(); return Done{report_.status}; }
    Step click(const View& view,Point point,std::string message,bool confirmation=false) {
        acted_=true; report_.status=message; publish();
        const auto& text=view.observations->text;
        return UseControl{{Knowledge::present,point,confirmation?view.observations->confirmation.captured:text.captured,view.context.window,view.context.generation},0,std::move(message),game::pointer_rest(view.context,*view.observations)};
    }
public:
    Rolling(std::shared_ptr<RollProgress> progress):progress_(std::move(progress)) {
        if (!progress_) throw std::invalid_argument("Roll progress is required");
        report_.status="Waiting for the open result"; publish();
    }
    RunConfig config() const override { return {std::chrono::hours(8),28,{},true,true}; }
    std::vector<Rect> text_regions() const override { return {{.1,0,.8,.95}}; }
    void step_completed(const View& view) override {
        if (!acted_) return;
        acted_=false; minimum_=view.context.now; frame_=minimum_; deadline_=minimum_+Time(8000);
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
    Ssa(SsaPlan plan,std::shared_ptr<RollProgress> progress):Rolling(std::move(progress)),plan_(std::move(plan)) {
        validate(plan_.filter);
        if (!plan_.honey_billions || plan_.honey_billions>1000000) throw std::invalid_argument("Set a finite honey limit in billions");
    }
    std::vector<game::FixedLabelQuery> fixed_labels() const override { return {{{.1,.1,.8,.85},{"Keep Old","Replace"}}}; }
    Step next(const View& view) override {
        if (report_.matched) return Done{report_.status};
        if (!fresh(view)) return waiting(view,"Reading SSA");
        const auto& c=view.context;
        const auto& state=*view.observations;
        const auto purchase=game::read_dialog(c,state.text,{{"guarantee 2 passive abilities"},{.15,.15,.7,.7},.25f,game::PromptMatch::phrase});
        const auto result=read_amulet(c,state);
        if (!purchase.has_value(c,Time(2000),minimum_) && stage_!=Stage::purchase && result.has_value(c,Time(2000),minimum_)) {
            report_.result.clear(); for (const auto& name:result.value.passives) { if (!report_.result.empty()) report_.result+=" / "; report_.result+=name; }
            if (matches(result.value,plan_.filter)) {
                finish("Match found; choose Keep Old or Replace in-game",true);
                return ReportActivity{{"SSA match found",report_.result+" · Choose Keep Old or Replace in-game"},result.value.popup};
            }
            stage_=Stage::purchase; pressed_=false;
        }
        const unsigned cost=plan_.filter.double_passive?500:10;
        if (stage_==Stage::purchase || stage_==Stage::current) {
            if (report_.attempts>=plan_.honey_billions/cost) return finish("Honey limit reached");
            if (purchase.has_value(c,Time(2000),minimum_)) {
                ++report_.attempts; stage_=Stage::result; pressed_=false;
                return click(view,plan_.filter.double_passive?purchase.value.yes:purchase.value.no,"Roll SSA");
            }
            if (!pressed_ && (result.has_value(c,Time(2000),minimum_) || game::read_prompt_landmark(c,state.text,
                {{"Generate a Supreme Star Amulet"},{.15,0,.7,.3},.5f,game::PromptMatch::phrase}).has_value(c,Time(2000),minimum_))) {
                stage_=Stage::purchase; pressed_=true; acted_=true;
                return Hold{{Key::interact},Time(80)};
            }
        }
        return waiting(view,"Waiting for the SSA result");
    }
};
class Hatcher final : public Rolling {
    HatchPlan plan_;
    std::shared_ptr<const game::InventorySource> inventory_;
    enum class Stage { drag,confirm,result } stage_{Stage::drag};
    bool jelly_{};
    std::optional<Bee> previous_;
public:
    Hatcher(HatchPlan plan,std::shared_ptr<RollProgress> progress):Rolling(std::move(progress)),plan_(std::move(plan)) {
        if (!plan_.target.valid() || !plan_.inventory || !plan_.eggs || plan_.eggs>1000000)
            throw std::invalid_argument("Select a bee and set a Basic Egg limit");
        inventory_=inventory_target(plan_.inventory,plan_.target);
        jelly_=plan_.start_with_jelly;
    }
    std::vector<Rect> text_regions() const override { return stage_==Stage::result?Rolling::text_regions():std::vector<Rect>{}; }
    std::vector<game::FixedLabelQuery> fixed_labels() const override {
        return stage_==Stage::result?std::vector<game::FixedLabelQuery>{{{.2,.15,.6,.8},{"Use Another"},game::LabelInk::dark}}:std::vector<game::FixedLabelQuery>{};
    }
    Step next(const View& view) override {
        if (!plan_.target.belongs_to(view.context.window)) return finish("Game window changed; select the bee again");
        if (stage_==Stage::drag) {
            if (report_.attempts>=plan_.eggs) return finish("Basic Egg limit reached");
            stage_=Stage::confirm;
            return DragInventoryItem{{{jelly_?"Royal Jelly":"Basic Egg"},"hive.bee"},inventory_};
        }
        if (!fresh(view,stage_==Stage::confirm)) return waiting(view,"Reading bee result");
        if (stage_==Stage::confirm) {
            const auto& dialog=view.observations->confirmation;
            if (!dialog.has_value(view.context,Time(2000),minimum_)) return waiting(view,"Waiting for egg confirmation");
            if (!jelly_) ++report_.attempts;
            stage_=Stage::result;
            return click(view,dialog.value.yes,jelly_?"Replace Basic Bee with Royal Jelly":"Hatch Basic Egg",true);
        }
        const auto result=read_jelly(view.context,*view.observations);
        if (result.knowledge==Knowledge::absent) previous_.reset();
        if (!result.has_value(view.context,Time(2000),minimum_)) return waiting(view,"Waiting for hatched bee");
        const auto& bee=result.value.bee;
        if (previous_ && bee.name==previous_->name && bee.gifted==previous_->gifted) return waiting(view,"Waiting for the new bee");
        report_.result=(bee.gifted?"Gifted ":"")+bee.name+" Bee"; publish();
        if ((bee.name=="Basic" && bee.gifted) || (bee.gifted && plan_.keep_gifted) || (bee.mythic && plan_.keep_mythic))
            return finish("Match found; bee left unchanged",true);
        previous_=bee; jelly_=bee.name=="Basic"; stage_=Stage::drag;
        return next(view);
    }
    void step_completed(const View& view) override {
        if (stage_==Stage::confirm) { minimum_=view.context.now; frame_=minimum_; deadline_=minimum_+Time(8000); }
        Rolling::step_completed(view);
    }
};
class Jelly final : public Rolling {
    JellyPlan plan_;
    std::shared_ptr<const game::InventorySource> inventory_;
    std::shared_ptr<FeedReceipt> receipt_;
    enum class Stage { result,feed,restart,confirm } stage_{Stage::result};
    std::optional<Bee> previous_;
    Time next_neon_{};
    Step feed_neon() {
        if (report_.neonberries>=plan_.neon_limit) return finish("Neonberry limit reached");
        receipt_=std::make_shared<FeedReceipt>(); stage_=Stage::feed;
        report_.status="Feeding one Neonberry"; publish();
        return RunProgram{[inventory=inventory_,receipt=receipt_] { return feed_item("Neonberry",1,inventory,receipt); }};
    }
public:
    Jelly(JellyPlan plan,std::shared_ptr<RollProgress> progress):Rolling(std::move(progress)),plan_(std::move(plan)) {
        validate(plan_.filter,plan_.require_mutation);
        if (plan_.require_mutation) validate(plan_.mutations);
        if (!plan_.rolls || plan_.rolls>1000000) throw std::invalid_argument("Set a finite Royal Jelly roll limit");
        if (plan_.neon) {
            if (!plan_.require_mutation || !plan_.target || !plan_.target->valid() || !plan_.inventory || !plan_.neon_limit || plan_.neon_limit>1000)
                throw std::invalid_argument("Automatic Neonberries need mutation rolling, a selected bee and a Neonberry limit");
            inventory_=inventory_target(plan_.inventory,*plan_.target);
        }
    }
    std::vector<Rect> text_regions() const override { return stage_==Stage::result?Rolling::text_regions():std::vector<Rect>{}; }
    std::vector<game::FixedLabelQuery> fixed_labels() const override {
        return stage_==Stage::result?std::vector<game::FixedLabelQuery>{{{.2,.15,.6,.8},{"Use Another"},game::LabelInk::dark}}:std::vector<game::FixedLabelQuery>{};
    }
    Step next(const View& view) override {
        if (plan_.neon && !plan_.target->belongs_to(view.context.window)) return finish("Game window changed; select the bee again");
        if (stage_==Stage::restart) {
            stage_=Stage::confirm;
            return DragInventoryItem{{{"Royal Jelly"},"hive.bee"},inventory_};
        }
        if (stage_==Stage::confirm) {
            if (!fresh(view,true)) return waiting(view,"Reading Royal Jelly confirmation");
            const auto& dialog=view.observations->confirmation;
            if (!dialog.has_value(view.context,Time(2000),minimum_)) return waiting(view,"Waiting for Royal Jelly confirmation");
            ++report_.attempts; stage_=Stage::result;
            previous_.reset();
            return click(view,dialog.value.yes,"Use Royal Jelly",true);
        }
        if (!fresh(view)) return waiting(view,"Reading bee result");
        const auto result=read_jelly(view.context,*view.observations);
        if (result.knowledge==Knowledge::absent) previous_.reset();
        if (plan_.neon && !report_.neonberries && result.knowledge==Knowledge::absent) return feed_neon();
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
        if (previous_ && previous_->name==bee.name && previous_->gifted==bee.gifted)
            return waiting(view,"Waiting for the next bee result");
        previous_.reset();
        if (report_.attempts>=plan_.rolls) return finish("Royal Jelly roll limit reached");
        if (plan_.neon && view.context.now>=next_neon_) return feed_neon();
        if (!result.value.again) return waiting(view,"Waiting for the Royal Jelly button");
        ++report_.attempts;
        previous_=bee;
        return click(view,*result.value.again,"Roll Royal Jelly");
    }
    void step_completed(const View& view) override {
        if (stage_==Stage::feed) {
            if (!receipt_ || !receipt_->completed || receipt_->amount!=1) throw std::runtime_error("Neonberry feed was not confirmed; no Royal Jelly submitted");
            ++report_.neonberries; next_neon_=view.context.now+std::chrono::minutes(11); receipt_.reset();
            stage_=Stage::restart; publish();
        } else if (stage_==Stage::confirm) {
            minimum_=view.context.now; deadline_=minimum_+Time(8000);
        }
        Rolling::step_completed(view);
    }
};
}
std::unique_ptr<automation::Program> hatch_basic(HatchPlan plan,std::shared_ptr<RollProgress> progress) { return std::make_unique<Hatcher>(std::move(plan),std::move(progress)); }
std::unique_ptr<automation::Program> roll_ssa(SsaPlan plan,std::shared_ptr<RollProgress> progress) { return std::make_unique<Ssa>(std::move(plan),std::move(progress)); }
std::unique_ptr<automation::Program> roll_jelly(JellyPlan plan,std::shared_ptr<RollProgress> progress) { return std::make_unique<Jelly>(std::move(plan),std::move(progress)); }
}
