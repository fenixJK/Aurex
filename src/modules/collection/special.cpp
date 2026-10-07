#include "hive/modules/collection/items.hpp"
#include "hive/game/detection/dialog.hpp"
#include "hive/game/data/stations.hpp"
#include <algorithm>

namespace hive::features::collection {
namespace {
using namespace automation;
class Printer final : public Program {
    std::shared_ptr<const game::DetectionSource> source_;
    std::string egg_;
    unsigned choice_{},phase_{};
    bool row_reset_{};
    Time minimum_{},started_{};
public:
    Printer(std::shared_ptr<const game::DetectionSource> source,std::string egg):source_(std::move(source)),egg_(std::move(egg)) {
        const std::vector<std::string> eggs{"Basic Egg","Silver Egg","Gold Egg","Diamond Egg","Mythic Egg"};
        const auto found=std::find(eggs.begin(),eggs.end(),egg_);
        if (!source_ || found==eggs.end()) throw std::invalid_argument("Choose a supported printer egg");
        choice_=unsigned(found-eggs.begin());
    }
    RunConfig config() const override { return {Time(45000),28,{},true,true}; }
    std::vector<Rect> text_regions() const override { return {{.1,.1,.8,.85}}; }
    ActivityStatus activity_status() const override { return {"Printing sticker",egg_}; }
    Step next(const View& view) override {
        const auto& c=view.context;
        if (!phase_) { phase_=1; started_=minimum_=c.now; return Hold{{Key::interact},Time(50)}; }
        if (c.now-started_>=Time(40000)) throw ObservationUnavailable("Sticker Printer did not finish");
        const auto packet=source_->read();
        if (!packet || !packet->error.empty() || !Observation<bool>{Knowledge::present,true,packet->context.now,packet->context.window,packet->context.generation}.has_value(c,Time(2000),minimum_)) return Wait{};
        const auto menu=std::dynamic_pointer_cast<const game::ItemMenu>(packet->value);
        if (!menu) return Wait{};
        const auto click=[&](Point point,std::string label) -> Step {
            minimum_=c.now+Time(100);
            return UseControl{{Knowledge::present,point,packet->context.now,c.window,c.generation},0,std::move(label),Point{.85,.15}};
        };
        if (phase_==1 && menu->kind==game::ItemMenuKind::sticker_printer) {
            if (menu->remaining && *menu->remaining>Time{}) throw ObservationUnavailable("Sticker Printer is on cooldown");
            if (menu->choices.size()<=choice_) return Wait{};
            if (!row_reset_) {
                row_reset_=true; minimum_=c.now+Time(150);
                return UseControl{{Knowledge::present,menu->choices.front(),packet->context.now,c.window,c.generation},4,"Scroll eggs to the start",Point{.85,.15}};
            }
            phase_=2; return click(menu->choices[choice_],"Choose "+egg_);
        }
        if (phase_==2 && menu->submit && menu->enabled) {
            if (!*menu->enabled) throw ObservationUnavailable("No "+egg_+" available for printing");
            phase_=3; return click(*menu->submit,"Print with "+egg_);
        }
        if (phase_==3 && view.observations) {
            const game::PromptLandmark question{{"Sticker"},{.1,.1,.8,.85},.7f,game::PromptMatch::phrase};
            const auto dialog=game::read_dialog(c,view.observations->text,question);
            if (!dialog.has_value(c,Time(2000),minimum_)) return Wait{};
            const auto expected=game::read_text_target(c,*view.observations,{egg_,{.1,.1,.8,.85}},minimum_);
            const auto gifted=game::read_text_target(c,*view.observations,{"Gifted",{.1,.1,.8,.85}},minimum_);
            if (!expected.has_value(c,Time(2000)) || gifted.has_value(c,Time(2000))) {
                phase_=5; return click(dialog.value.no,"Cancel unexpected printer egg");
            }
            phase_=4; return click(dialog.value.yes,"Confirm "+egg_);
        }
        if (phase_==4 && view.observations) {
            const auto remaining=game::read_text_cooldown(c,*view.observations,game::station_target("Sticker Printer",true),minimum_);
            if (remaining.has_value(c,Time(2000))) phase_=6;
        }
        if (phase_==4 && menu->remaining && *menu->remaining>Time{}) {
            phase_=6;
            if (menu->close) return click(*menu->close,"Close Sticker Printer");
        }
        if (phase_==5) return Defer{"Printer egg could not be confirmed"};
        if (phase_==6) return Done{"Sticker printed"};
        return Wait{Time(100),"Waiting for Sticker Printer"};
    }
};
class CenterDrop final : public game::InventorySource {
    std::shared_ptr<const game::InventorySource> source_;
public:
    explicit CenterDrop(std::shared_ptr<const game::InventorySource> source):source_(std::move(source)) {
        if (!source_) throw std::invalid_argument("Glue collection needs inventory observations");
    }
    Observation<game::InventoryView> read(const ObservationContext& c,const game::State& state) const override {
        auto value=source_->read(c,state);
        if (value.has_value(c,Time(2000))) {
            value.value.target="Gummy Bee"; value.value.drop.reset();
            const auto& box=value.value.viewport;
            if (!value.value.open || .5<box.x || .5>box.x+box.width || .5<box.y || .5>box.y+box.height) value.value.drop=Point{.5,.5};
        }
        return value;
    }
};
class Glue final : public Program {
    std::shared_ptr<const game::InventorySource> inventory_;
    std::shared_ptr<const movement::Recipes> movement_;
    double speed_;
    unsigned phase_{};
public:
    Glue(std::shared_ptr<const game::InventorySource> inventory,double speed,movement::Source movement):inventory_(std::make_shared<CenterDrop>(std::move(inventory))),movement_(movement::snapshot(movement)),speed_(speed) {}
    RunConfig config() const override { return {Time(90000),speed_,{},true,true}; }
    Step next(const View& view) override {
        if (!phase_++) return DragInventoryItem{{{"Gumdrops"},"Gummy Bee"},inventory_};
        if (phase_==2 && view.observations) {
            const auto inventory=inventory_->read(view.context,*view.observations);
            if (inventory.has_value(view.context,Time(2000)) && inventory.value.open && inventory.value.toggle)
                return UseControl{{Knowledge::present,*inventory.value.toggle,inventory.captured,inventory.window,inventory.generation},0,"Close inventory",Point{.85,.15}};
        }
        if (phase_<=3) { phase_=3; return FollowPath{movement_->path("gummy-entry",{},speed_).at(0)}; }
        if (phase_==4) return UseStation{"Glue Dispenser"};
        return Done{"Collected Glue"};
    }
};
}
std::unique_ptr<automation::Program> print_sticker(std::shared_ptr<const game::DetectionSource> source,std::string egg) {
    return std::make_unique<Printer>(std::move(source),std::move(egg));
}
std::unique_ptr<automation::Program> collect_glue(std::shared_ptr<const game::InventorySource> inventory,double speed,movement::Source movement) {
    return std::make_unique<Glue>(std::move(inventory),speed,std::move(movement));
}
}
