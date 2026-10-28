#include "item_reserves.hpp"

namespace hive::automation {
namespace {
class InspectReserves final : public Program {
    std::shared_ptr<game::ItemStock> stock_;
    std::shared_ptr<const game::InventorySource> inventory_;
    game::ItemAmounts cost_;
    std::vector<std::string> unread_;
    std::size_t index_{};
    std::shared_ptr<Observation<workflows::InventoryMatch>> reading_;
    std::string issue_;
    bool closing_{};
public:
    InspectReserves(std::shared_ptr<game::ItemStock> stock,std::shared_ptr<const game::InventorySource> inventory,game::ItemAmounts cost)
        :stock_(std::move(stock)),inventory_(std::move(inventory)),cost_(std::move(cost)),unread_(stock_->unread(cost_)) {}
    RunConfig config() const override { return {std::chrono::minutes(5),28,{},true,true}; }
    game::ObservationNeeds observation_needs() const override { return closing_?game::ObservationNeeds{}:game::ObservationNeeds{game::ObservationKind::menus}; }
    ActivityStatus activity_status() const override { return {"Checking supplies",index_<unread_.size()?unread_[index_]:std::string{}}; }
    Step next(const View& view) override {
        if (reading_) {
            if (!reading_->has_value(view.context,Time(2000)) || !reading_->value.item.quantity)
                issue_="Inventory count unavailable for "+unread_[index_];
            else stock_->observe(unread_[index_],reading_->value.item.quantity->count,reading_->captured);
            reading_.reset(); ++index_;
        }
        if (issue_.empty() && index_<unread_.size()) {
            reading_=std::make_shared<Observation<workflows::InventoryMatch>>();
            return InspectInventoryItem{{unread_[index_]},inventory_,reading_,false,false,true};
        }
        if (!closing_ && !unread_.empty() && view.observations && view.observations->menus.has_value(view.context,Time(2000))) {
            closing_=true;
            for (const auto& menu:view.observations->menus.value) if (menu.tab==game::MenuTab::inventory && menu.selected==true)
                return UseControl{{Knowledge::present,menu.center(),view.observations->menus.captured,view.context.window,view.context.generation},0,"Close inventory",{},true};
        }
        if (!issue_.empty()) return Defer{issue_};
        if (const auto blocked=stock_->blocked(cost_)) return Defer{"Keeping the reserved "+*blocked};
        return Done{"Supplies checked"};
    }
};
}
std::unique_ptr<Program> inspect_reserves(std::shared_ptr<game::ItemStock> stock,std::shared_ptr<const game::InventorySource> inventory,game::ItemAmounts cost) {
    if (!stock || !inventory) throw std::invalid_argument("Item reserves need inventory observations");
    return std::make_unique<InspectReserves>(std::move(stock),std::move(inventory),std::move(cost));
}
}
