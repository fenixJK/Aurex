#include "rolling.hpp"
#include "game/actions/inventory/feeding.hpp"
#include "game/detection/menus.hpp"
#include <algorithm>

namespace hive::features::tools {
namespace {
using namespace automation;
class Bitterberries final : public Program {
    BitterPlan plan_;
    std::shared_ptr<RollProgress> progress_;
    RollReport report_;
    std::shared_ptr<const game::InventorySource> inventory_;
    std::shared_ptr<FeedReceipt> receipt_;
    enum class Stage { select,initial,close,feed,result } stage_{Stage::select};
    Time minimum_{},deadline_{},next_neon_{};
    bool feeding_neon_{},acted_{};
    void publish() { progress_->set(report_); }
    Step done(std::string status,bool matched=false) { report_.status=std::move(status);report_.matched=matched;publish();return Done{report_.status}; }
    Step wait(const View& view,const char* reason) {
        if (view.context.now>=deadline_) return done("Could not read the bee result; no further berries submitted");
        return Wait{Time(100),reason};
    }
public:
    Bitterberries(BitterPlan plan,std::shared_ptr<RollProgress> progress):plan_(std::move(plan)),progress_(std::move(progress)) {
        validate(plan_.mutations);
        if (!progress_ || !plan_.target.valid() || !plan_.inventory || !plan_.batch || plan_.batch>1000000
            || !plan_.limit || plan_.limit>1000000 || (plan_.neon && (!plan_.neon_limit || plan_.neon_limit>1000)))
            throw std::invalid_argument("Invalid Bitterberry setup");
        inventory_=inventory_target(plan_.inventory,plan_.target); report_.status="Checking selected bee"; publish();
    }
    RunConfig config() const override { return {std::chrono::hours(8),28,{},true,true}; }
    std::vector<Rect> text_regions() const override {
        if (stage_==Stage::select || stage_==Stage::feed || (stage_==Stage::result && receipt_ && !receipt_->completed)) return {};
        return {{.02,.12,.93,.78}};
    }
    Step next(const View& view) override {
        const auto& c=view.context;
        if (!plan_.target.belongs_to(c.window)) return done("Game window changed; select the bee again");
        if (stage_==Stage::select) {
            stage_=Stage::initial; acted_=true;
            return UseControl{{Knowledge::present,plan_.target.point,c.now,c.window,c.generation},0,"Inspect selected bee",Point{.85,.15}};
        }
        if (stage_==Stage::feed) {
            if (report_.bitterberries>=plan_.limit) return done("Bitterberry limit reached");
            feeding_neon_=plan_.neon && c.now>=next_neon_;
            if (feeding_neon_ && report_.neonberries>=plan_.neon_limit) return done("Neonberry limit reached");
            const auto amount=feeding_neon_?1:std::min(plan_.batch,plan_.limit-report_.bitterberries);
            const std::string food=feeding_neon_?"Neonberry":"Bitterberry";
            receipt_=std::make_shared<FeedReceipt>(); stage_=Stage::result;
            report_.status="Feeding "+std::to_string(amount)+" "+food; publish();
            return RunProgram{[food,amount,inventory=inventory_,receipt=receipt_] {return feed_item(food,amount,inventory,receipt);}};
        }
        if (!view.observations || !view.observations->text.has_value(c,Time(2000),minimum_)
            || view.observations->text.captured<=minimum_) return wait(view,"Reading bee result");
        const auto panel=read_mutation(c,*view.observations);
        if (!panel.usable(c,Time(2000),minimum_)) return wait(view,"Reading mutation");
        if (stage_==Stage::close) {
            if (panel.value.bee_menu || panel.knowledge==Knowledge::present) return wait(view,"Closing bee result");
            stage_=Stage::feed; return Wait{Time(1),"Bee result closed"};
        }
        if (stage_==Stage::initial && !panel.value.bee_menu) return wait(view,"Opening selected bee");
        if (stage_==Stage::result && !feeding_neon_ && panel.value.bee_menu && panel.knowledge==Knowledge::absent)
            return wait(view,"Reading the new mutation panel");
        if (panel.knowledge==Knowledge::present) {
            report_.result=describe(panel.value.mutation); publish();
            if (matches(panel.value.mutation,plan_.mutations)) return done("Mutation found; bee result left open",true);
        }
        if (panel.value.bee_menu || panel.knowledge==Knowledge::present) {
            if (!panel.value.close) return wait(view,"Locating bee close button");
            stage_=Stage::close; acted_=true;
            return UseControl{{Knowledge::present,*panel.value.close,panel.captured,c.window,c.generation},0,"Close bee result",game::pointer_rest(c,*view.observations)};
        }
        stage_=Stage::feed; return Wait{Time(1),"Ready for next feed"};
    }
    void step_completed(const View& view) override {
        if (receipt_) {
            if (!receipt_->completed) throw std::runtime_error("Feed outcome was not confirmed; stopped before another batch");
            if (feeding_neon_) { report_.neonberries+=receipt_->amount; next_neon_=view.context.now+std::chrono::minutes(11); }
            else { report_.bitterberries+=receipt_->amount; ++report_.attempts; }
            receipt_.reset(); publish(); minimum_=view.context.now; deadline_=minimum_+Time(8000);
        }
        if (acted_) {acted_=false;minimum_=view.context.now;deadline_=minimum_+Time(8000);}
    }
    void finished(State state) override {
        if (state==State::stopped || state==State::failed) {report_.status=state==State::stopped?"Stopped":"Stopped after an error";publish();}
    }
};
}
std::unique_ptr<automation::Program> roll_mutations(BitterPlan plan,std::shared_ptr<RollProgress> progress) {
    return std::make_unique<Bitterberries>(std::move(plan),std::move(progress));
}
}
