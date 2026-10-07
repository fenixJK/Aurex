#include "hive/automation/feeding.hpp"
#include "hive/automation/shift_lock.hpp"
#include "hive/game/detection/feeding.hpp"
#include <algorithm>

namespace hive::automation {
namespace {
class SelectedDestination final : public game::InventorySource {
    std::shared_ptr<const game::InventorySource> source_;
    WindowPoint target_;
public:
    SelectedDestination(std::shared_ptr<const game::InventorySource> source,WindowPoint target):source_(std::move(source)),target_(std::move(target)) {
        if (!source_ || !target_.valid()) throw std::invalid_argument("Choose a target in the game window");
    }
    Observation<game::InventoryView> read(const ObservationContext& c,const game::State& state) const override {
        if (!target_.belongs_to(c.window)) throw std::runtime_error("Game window changed; select the bee again");
        auto view=source_->read(c,state);
        if (view.has_value(c,Time(2000))) {
            const auto& r=view.value.viewport; const auto p=target_.point;
            if (view.value.open && p.x>=r.x && p.x<r.x+r.width && p.y>=r.y && p.y<r.y+r.height)
                throw std::runtime_error("Inventory covers the selected bee; move the camera and select it again");
            view.value.drop=p; view.value.target="hive.bee";
        }
        return view;
    }
};
class Destination final : public game::InventorySource {
    FeedingPlan plan_;
public:
    explicit Destination(FeedingPlan plan):plan_(std::move(plan)) {}
    Observation<game::InventoryView> read(const ObservationContext& c,const game::State& state) const override {
        auto inventory=plan_.inventory->read(c,state);
        const auto visible=plan_.reference->read(c); const auto packet=plan_.hive->read();
        if (!inventory.has_value(c,Time(2000)) || !visible.has_value(c,Time(2000)) || !visible.value || !packet || !packet->error.empty()) return inventory;
        const auto hive=std::dynamic_pointer_cast<const game::HiveViewDetection>(packet->value);
        if (!hive || !hive->region.has_value(c,Time(2000))) return inventory;
        const auto r=hive->region.value;
        const Point point{r.x+r.width*.5,r.y+r.height*.5};
        if (point.x<=inventory.value.viewport.x+inventory.value.viewport.width || point.x>=1 || point.y<0 || point.y>=1) return inventory;
        inventory.value.drop=point; inventory.value.target="hive.bee"; return inventory;
    }
};
class Feeding final : public Program {
    FeedingPlan plan_;
    enum class Stage { setup,position,inspect,drag,dialog,verify,closing,inventory,done } stage_{Stage::setup};
    std::shared_ptr<Observation<workflows::InventoryMatch>> item_{std::make_shared<Observation<workflows::InventoryMatch>>()};
    Time minimum_{},deadline_{},frame_{Time(-1)};
    unsigned amount_{};
    bool action_{};
    std::shared_ptr<FeedReceipt> receipt_;
    Time settle_{100};
    Step wait(const View& view,const std::string& message) {
        if (view.context.now>=deadline_) throw std::runtime_error(message+" timed out; no additional feed was submitted");
        return Wait{Time(100),message};
    }
public:
    explicit Feeding(FeedingPlan plan):plan_(std::move(plan)) {
        if ((plan_.food!="Strawberry" && plan_.food!="Blueberry") || !plan_.amount || plan_.amount>100 || !plan_.prepare
            || !plan_.inventory || !plan_.hive || !plan_.reference) throw std::invalid_argument("Feeding needs a bounded amount, inventory and own-hive observations");
    }
    Feeding(std::string food,unsigned amount,std::shared_ptr<const game::InventorySource> inventory,std::shared_ptr<FeedReceipt> receipt,Time settle)
        :stage_(Stage::drag),amount_(amount),receipt_(std::move(receipt)),settle_(settle) {
        if ((food!="Bitterberry" && food!="Neonberry") || !amount || amount>1000000 || !inventory || !receipt_
            || settle<Time(500) || settle>Time(5000)) throw std::invalid_argument("Feeding needs a supported food, amount and destination");
        plan_.food=std::move(food); plan_.inventory=std::move(inventory);
    }
    RunConfig config() const override { return {std::chrono::minutes(6),28,{},true,true}; }
    std::vector<Rect> text_regions() const override {
        return stage_==Stage::dialog || stage_==Stage::verify || stage_==Stage::closing?std::vector<Rect>{{.2,.15,.6,.75}}:std::vector<Rect>{};
    }
    Step next(const View& view) override {
        const auto& c=view.context;
        if (stage_==Stage::setup) { stage_=Stage::position; return RunProgram{[] { return set_shift_lock(false); }}; }
        if (stage_==Stage::position) { stage_=Stage::inspect; return RunProgram{plan_.prepare}; }
        if (stage_==Stage::inspect) { stage_=Stage::drag; return InspectInventoryItem{{plan_.food},plan_.inventory,item_}; }
        if (stage_==Stage::drag) {
            if (!receipt_) {
                if (!item_->has_value(c,Time(2000)) || !item_->value.item.quantity) return Done{"Food quantity could not be confirmed"};
                amount_=unsigned(std::min<std::uint64_t>(plan_.amount,item_->value.item.quantity->count));
                if (!amount_) return Done{"No food is available"};
            }
            stage_=Stage::dialog; action_=true;
            return DragInventoryItem{{{plan_.food},"hive.bee"},receipt_?plan_.inventory:std::make_shared<Destination>(plan_)};
        }
        if (stage_==Stage::done) return Done{"Feeding finished; quest progress must be checked"};
        if (!view.observations) return wait(view,"Waiting for feeding controls");
        if (stage_==Stage::inventory) {
            const auto inventory=plan_.inventory->read(c,*view.observations);
            if (!inventory.has_value(c,Time(2000),minimum_)) return wait(view,"Reading inventory after feeding");
            stage_=Stage::done;
            if (inventory.value.open && inventory.value.toggle) return UseControl{{Knowledge::present,*inventory.value.toggle,inventory.captured,c.window,c.generation},0,"Close inventory"};
            return Done{"Feeding finished; quest progress must be checked"};
        }
        const auto dialog=game::read_feeding_dialog(c,*view.observations);
        if (!dialog.usable(c,Time(2000),minimum_) || dialog.captured<=frame_) return wait(view,"Reading feeding dialog");
        frame_=dialog.captured;
        if (stage_==Stage::closing) {
            if (dialog.knowledge==Knowledge::absent) {
                if (receipt_) { receipt_->completed=true; return Done{"Feed submitted"}; }
                stage_=Stage::inventory; deadline_=c.now+Time(10000);
            }
            return wait(view,"Confirming feed result");
        }
        if (dialog.knowledge!=Knowledge::present) return wait(view,"Locating feeding dialog");
        if (stage_==Stage::dialog) {
            stage_=Stage::verify; action_=true;
            return EnterNumber{{Knowledge::present,dialog.value.input,dialog.captured,c.window,c.generation},amount_};
        }
        if (dialog.value.amount!=amount_) return wait(view,"Waiting for the complete feeding amount");
        stage_=Stage::closing; action_=true;
        if (receipt_) receipt_->amount=amount_;
        return UseControl{{Knowledge::present,dialog.value.feed,dialog.captured,c.window,c.generation},0,"Feed "+std::to_string(amount_)+" "+plan_.food,Point{.85,.15}};
    }
    void step_completed(const View& view) override {
        if (!action_) return;
        action_=false; minimum_=view.context.now+(stage_==Stage::closing?settle_:Time(100)); deadline_=minimum_+Time(10000);
    }
};
}
std::unique_ptr<Program> feed_bees(FeedingPlan plan) { return std::make_unique<Feeding>(std::move(plan)); }
std::shared_ptr<const game::InventorySource> inventory_target(std::shared_ptr<const game::InventorySource> source,WindowPoint target) {
    return std::make_shared<SelectedDestination>(std::move(source),std::move(target));
}
std::unique_ptr<Program> feed_item(std::string food,unsigned amount,std::shared_ptr<const game::InventorySource> inventory,std::shared_ptr<FeedReceipt> receipt,Time settle) {
    return std::make_unique<Feeding>(std::move(food),amount,std::move(inventory),std::move(receipt),settle);
}
}
