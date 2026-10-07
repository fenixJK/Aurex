#pragma once
#include "hive/game/detection/dialog.hpp"
#include "hive/core/tasks.hpp"

namespace hive::workflows {
enum class DialogChoice { yes,no };
struct DialogPlan { game::PromptLandmark question; DialogChoice choice{DialogChoice::yes}; Time timeout{10000}; bool read_question{true}; };
void validate(const DialogPlan& plan);
enum class DialogStage { idle,reading,clicking,closing,completed,failed };
class Dialog {
    DialogPlan plan_;
    ObservationContext started_;
    DialogStage stage_{DialogStage::idle};
    Time last_{},minimum_{},frame_{Time(-1)};
    bool owned_{},timed_out_{};
    std::string pending_,reason_;
    void finish(DialogStage stage,std::string reason,TaskPort& tasks,ModuleScope scope);
public:
    explicit Dialog(DialogPlan plan);
    void start(const ObservationContext& context);
    void update(const ObservationContext& context,const game::State& state,TaskPort& tasks,ModuleScope scope);
    DialogStage stage() const { return stage_; }
    bool timed_out() const { return timed_out_; }
    const std::string& reason() const { return reason_; }
};
}
