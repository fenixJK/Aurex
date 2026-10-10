#include "ant_pass.hpp"

namespace hive::features::collection {
void AntPassBudget::configure(unsigned limit,std::uint64_t session) {
    if (limit>100) throw std::invalid_argument("Ant Pass purchase limit is too large");
    std::lock_guard lock(mutex_);
    if (session_!=session) { session_=session; spent_=0; }
    limit_=limit;
}
void AntPassBudget::disable() { std::lock_guard lock(mutex_); limit_=0; }
bool AntPassBudget::available() const { std::lock_guard lock(mutex_); return session_ && spent_<limit_; }
bool AntPassBudget::reserve() { std::lock_guard lock(mutex_); if (!session_ || spent_>=limit_) return false; ++spent_; return true; }
namespace {
using namespace automation;
class Purchase final : public Program {
    std::shared_ptr<AntPassBudget> budget_;
    ProgramFactory prepare_,home_;
    std::shared_ptr<const game::InventorySource> inventory_;
    std::shared_ptr<Observation<workflows::InventoryMatch>> result_{std::make_shared<Observation<workflows::InventoryMatch>>()};
    double speed_;
    unsigned phase_{};
    Time submitted_{};
public:
    Purchase(std::shared_ptr<AntPassBudget> budget,ProgramFactory prepare,ProgramFactory home,std::shared_ptr<const game::InventorySource> inventory,double speed)
        :budget_(std::move(budget)),prepare_(std::move(prepare)),home_(std::move(home)),inventory_(std::move(inventory)),speed_(speed) {}
    RunConfig config() const override { return {std::chrono::minutes(5),speed_,{},true,true}; }
    ActivityStatus activity_status() const override { return {"Buying Ant Pass","10 tickets"}; }
    Step next(const View& view) override {
        if (phase_<4 && !budget_->available()) throw ObservationUnavailable("Ant Pass purchase limit reached");
        switch(phase_) {
            case 0: ++phase_; return RunProgram{home_};
            case 1: ++phase_; return RunProgram{prepare_};
            case 2: ++phase_; return Navigate{"station.ant-pass-paid"};
            case 3: {
                if (!budget_->reserve()) throw ObservationUnavailable("Ant Pass purchase limit reached");
                ++phase_; submitted_=view.context.now;
                return Hold{{Key::interact},Time(80)};
            }
            case 4: ++phase_; return InspectInventoryItem{{"Ant Pass"},inventory_,result_,false,false,true};
            case 5:
                if (!result_->has_value(view.context,Time(2000),submitted_) || !result_->value.item.quantity || !result_->value.item.quantity->count)
                    throw ObservationUnavailable("Ant Pass purchase was not confirmed; no further tickets spent");
                ++phase_; return RunProgram{home_};
            default: return Done{"Ant Pass ready"};
        }
    }
};
}
automation::ProgramFactory ant_pass_purchase(std::shared_ptr<AntPassBudget> budget,automation::ProgramFactory prepare,
    automation::ProgramFactory home,std::shared_ptr<const game::InventorySource> inventory,double speed) {
    if (!budget || !prepare || !home || !inventory) return {};
    return [budget,prepare,home,inventory,speed] {
        if (!budget->available()) throw automation::ObservationUnavailable("No Ant Pass available; ticket purchases are disabled or at their limit");
        return std::make_unique<Purchase>(budget,prepare,home,inventory,speed);
    };
}
}
