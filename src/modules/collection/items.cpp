#include "items.hpp"
#include "game/actions/inventory/item_reserves.hpp"
#include <algorithm>

namespace hive::features::collection {
namespace {
using namespace automation;
using game::ItemMenuKind;
using game::ItemMenuStage;
class ItemActivity final : public Program {
    std::shared_ptr<const game::DetectionSource> source_;
    std::shared_ptr<ItemResult> result_;
    ItemMenuKind kind_;
    std::string item_;
    unsigned amount_;
    double speed_;
    std::shared_ptr<const movement::Recipes> movement_;
    std::shared_ptr<ItemOrders> orders_;
    std::shared_ptr<game::ItemStock> stock_;
    std::shared_ptr<const game::InventorySource> inventory_;
    bool supplies_checked_{};
    game::ItemAmounts cost() const { return kind_==ItemMenuKind::blender?game::blender_cost(item_,amount_):game::ItemAmounts{{item_,amount_}}; }
    std::optional<ItemOrder> order_;
    Time shrine_interval_;
    bool collect_only_{};
    enum class Phase { opening,select,clearing,collecting,quantity,submitted,rewards,closing,loot,done } phase_{Phase::opening};
    bool began_{},entered_{},selected_{},clicked_{},accepted_{};
    unsigned pages_{},quantity_clicks_{},stalled_{},dialogue_pages_{};
    std::optional<unsigned> previous_quantity_;
    Observation<Point> quantity_button_;
    Time deadline_{},minimum_{};
    std::string name() const { return kind_==ItemMenuKind::blender?"Blender":"Wind Shrine"; }
    void choose_order() {
        if (!orders_) return;
        order_=orders_->next(kind_); collect_only_=!order_;
        if (order_) { item_=order_->item; amount_=order_->amount; }
        selected_=false; previous_quantity_.reset(); stalled_=quantity_clicks_=pages_=0;
    }
    void accept(Time delay) {
        if (accepted_) return;
        if (orders_ && order_) orders_->accepted(kind_,*order_,delay);
        accepted_=true;
    }
    Step missing(const game::ItemMenu& menu,const game::DetectionResult& packet) {
        if (!orders_) throw ObservationUnavailable(name()+": not enough materials for "+item_);
        orders_->missing(kind_,item_); choose_order();
        if (collect_only_) { result_->retry=std::chrono::hours(1); return close(menu,packet); }
        if (kind_==ItemMenuKind::blender && menu.stage==ItemMenuStage::quantity && menu.cancel) {
            phase_=Phase::clearing; return click(*menu.cancel,packet,"Choose another recipe");
        }
        phase_=Phase::select; return Wait{};
    }
    Step click(Point p,const game::DetectionResult& packet,std::string label) {
        clicked_=true;
        return UseControl{{Knowledge::present,p,packet.context.now,packet.context.window,packet.context.generation},0,std::move(label),{}};
    }
    Step close(const game::ItemMenu& menu,const game::DetectionResult& packet) {
        phase_=Phase::closing;
        if (menu.close) return click(*menu.close,packet,"Close Blender");
        if (menu.cancel) return click(*menu.cancel,packet,"Close item menu");
        return Wait{};
    }
    Step adjust(const game::ItemMenu& menu,const game::DetectionResult& packet) {
        if (!menu.quantity) return Wait{};
        if (*menu.quantity==amount_) {
            if (!menu.submit || !menu.enabled) return Wait{};
            if (!*menu.enabled) return missing(menu,packet);
            if (stock_ && !stock_->spend(cost(),packet.context.now)) {
                if (orders_) orders_->missing(kind_,item_);
                result_->retry=std::chrono::minutes(5); return close(menu,packet);
            }
            phase_=Phase::submitted;
            return click(*menu.submit,packet,kind_==ItemMenuKind::blender?"Start crafting":"Donate selected items");
        }
        if (previous_quantity_==menu.quantity) {
            if (++stalled_>=3) {
                if (orders_) return missing(menu,packet);
                throw ObservationUnavailable(name()+": quantity did not change");
            }
        } else stalled_=0;
        previous_quantity_=menu.quantity;
        const auto button=*menu.quantity<amount_?menu.more:menu.less;
        if (!button) return Wait{};
        quantity_clicks_=std::min(8u,*menu.quantity<amount_?amount_-*menu.quantity:*menu.quantity-amount_)-1;
        quantity_button_={Knowledge::present,*button,packet.context.now,packet.context.window,packet.context.generation};
        return click(*button,packet,"Set item quantity");
    }
    FollowPath loot() const { return {movement_->path("shrine-loot",{},speed_).at(0)}; }

public:
    ItemActivity(std::shared_ptr<const game::DetectionSource> source,ItemOptions options,std::shared_ptr<ItemResult> result,ItemMenuKind kind,double speed)
        :source_(std::move(source)),result_(std::move(result)),kind_(kind),item_(kind==ItemMenuKind::blender?options.recipe:options.donation),
         amount_(kind==ItemMenuKind::blender?options.batches:options.quantity),speed_(speed),movement_(movement::snapshot(options.movement)),orders_(std::move(options.orders)),stock_(std::move(options.stock)),inventory_(std::move(options.inventory)),shrine_interval_(options.shrine_interval) {
        choose_order();
        const auto& catalog=kind==ItemMenuKind::blender?game::blender_recipes():game::shrine_items();
        if (!source_ || !result_ || std::find(catalog.begin(),catalog.end(),item_)==catalog.end() || amount_<1 || amount_>999
            || !std::isfinite(speed_) || speed_<8 || speed_>200 || shrine_interval_<std::chrono::hours(1) || shrine_interval_>std::chrono::hours(24)) throw std::invalid_argument("Invalid item activity settings");
        result_->retry.reset();
    }
    RunConfig config() const override { return {std::chrono::minutes(8),speed_,{},true,true}; }
    ActivityStatus activity_status() const override {
        return {phase_==Phase::loot?"Collecting loot":kind_==ItemMenuKind::blender?"Crafting":"Donating",name()};
    }
    Step next(const View& view) override {
        const auto& c=view.context;
        if (phase_==Phase::done) return Done{result_->retry?name()+" deferred":name()+" finished"};
        if (orders_ && collect_only_ && kind_==ItemMenuKind::wind_shrine && !began_) return Done{"Donation queue finished"};
        if (phase_==Phase::loot) { phase_=Phase::done; return loot(); }
        if (!began_ && !collect_only_ && stock_ && !(orders_ && kind_==ItemMenuKind::blender && orders_->read().blender.crafting)) {
            const auto spending=cost();
            if (!supplies_checked_ && !stock_->unread(spending).empty()) {
                supplies_checked_=true;
                if (inventory_) return RunProgram{[stock=stock_,inventory=inventory_,spending] { return inspect_reserves(stock,inventory,spending); },true};
            }
            if (const auto item=stock_->blocked(spending)) {
                result_->retry=std::chrono::minutes(5);
                if (orders_) orders_->missing(kind_,item_);
                return Defer{"Keeping the reserved "+*item};
            }
        }
        if (!began_) { began_=true; minimum_=c.now; deadline_=c.now+Time(10000); }
        if (c.now>=deadline_) throw ObservationUnavailable(name()+": menu did not respond");
        if (quantity_clicks_) {
            if (!quantity_button_.has_value(c,Time(2000))) quantity_clicks_=0;
            else { --quantity_clicks_; clicked_=true; return UseControl{quantity_button_,0,"Set item quantity",{}}; }
        }
        const auto packet=source_->read();
        if (!packet || !packet->error.empty()) return Wait{};
        const Observation<bool> fresh{Knowledge::present,true,packet->context.now,packet->context.window,packet->context.generation};
        if (!fresh.has_value(c,Time(2000),minimum_)) return Wait{};
        const auto menu=std::dynamic_pointer_cast<const game::ItemMenu>(packet->value);
        if (!menu) return Wait{};
        if (!menu->error.empty()) throw ObservationUnavailable(name()+": "+menu->error);
        if (phase_==Phase::closing) {
            if (menu->kind==ItemMenuKind::none && !menu->continue_dialogue) { phase_=Phase::done; return next(view); }
            return Wait{};
        }
        if (menu->kind!=ItemMenuKind::none && menu->kind!=kind_) throw ObservationUnavailable("A different item menu is open");
        if (kind_==ItemMenuKind::wind_shrine && menu->continue_dialogue) {
            if (!entered_ && !menu->remaining && menu->dialogue.find("Shrine")==std::string::npos)
                throw ObservationUnavailable("A different conversation is open");
            entered_=true;
            if (menu->remaining && !accepted_) {
                result_->retry=std::max(Time(1000),*menu->remaining);
                if (orders_) orders_->defer(kind_,*result_->retry);
            }
            if (phase_==Phase::submitted && !result_->retry && menu->dialogue.find("offering")==std::string::npos) accept(shrine_interval_);
            if (accepted_ || result_->retry) phase_=Phase::rewards;
            if (++dialogue_pages_>100) throw ObservationUnavailable("Wind Shrine dialogue did not finish");
            return click(*menu->continue_dialogue,*packet,"Continue Wind Shrine dialogue");
        }
        if (phase_==Phase::rewards && menu->kind==ItemMenuKind::none) {
            phase_=accepted_?Phase::loot:Phase::done; return next(view);
        }
        if (menu->kind==ItemMenuKind::none) {
            if (!entered_) { entered_=clicked_=true; return Hold{{Key::interact},Time(50),{}}; }
            return Wait{};
        }
        entered_=true;
        if (kind_==ItemMenuKind::blender && menu->stage==ItemMenuStage::crafting) {
            if (phase_==Phase::submitted) { accept(menu->remaining.value_or(std::chrono::minutes(5)*amount_)); return close(*menu,*packet); }
            if (phase_==Phase::collecting) return Wait{};
            if (!menu->finished || !menu->submit) return Wait{};
            if (!*menu->finished) {
                result_->retry=std::max(Time(1000),menu->remaining.value_or(std::chrono::minutes(5)));
                if (orders_) orders_->defer(kind_,*result_->retry);
                return close(*menu,*packet);
            }
            phase_=Phase::collecting;
            return click(*menu->submit,*packet,"Collect finished crafting");
        }
        if (phase_==Phase::submitted) return Wait{};
        if (kind_==ItemMenuKind::blender && menu->stage==ItemMenuStage::selection && orders_) {
            orders_->collected();
            if (collect_only_) return close(*menu,*packet);
        }
        if (kind_==ItemMenuKind::blender && menu->stage==ItemMenuStage::quantity) {
            if (!selected_) {
                if (phase_==Phase::clearing) return Wait{};
                if (menu->cancel) { phase_=Phase::clearing; return click(*menu->cancel,*packet,"Clear unfinished recipe selection"); }
                return Wait{};
            }
            phase_=Phase::quantity; return adjust(*menu,*packet);
        }
        if (menu->item.empty()) return Wait{};
        if (menu->item!=item_) {
            if (!menu->next) return Wait{};
            if (++pages_>64) throw ObservationUnavailable(name()+": selected item was not found");
            return click(*menu->next,*packet,"Select "+item_);
        }
        if (kind_==ItemMenuKind::wind_shrine) { phase_=Phase::quantity; return adjust(*menu,*packet); }
        if (selected_) return Wait{};
        if (!menu->submit || !menu->enabled) return Wait{};
        if (!*menu->enabled) return missing(*menu,*packet);
        selected_=true;
        return click(*menu->submit,*packet,"Choose "+item_);
    }
    void step_completed(const View& view) override {
        if (!clicked_) return;
        clicked_=false; minimum_=view.context.now+Time(100); deadline_=view.context.now+Time(10000);
    }
};
}
std::unique_ptr<automation::Program> blend(std::shared_ptr<const game::DetectionSource> source,ItemOptions options,std::shared_ptr<ItemResult> result) {
    return std::make_unique<ItemActivity>(std::move(source),std::move(options),std::move(result),ItemMenuKind::blender,28);
}
std::unique_ptr<automation::Program> donate(std::shared_ptr<const game::DetectionSource> source,ItemOptions options,std::shared_ptr<ItemResult> result,double speed) {
    return std::make_unique<ItemActivity>(std::move(source),std::move(options),std::move(result),ItemMenuKind::wind_shrine,speed);
}
}
