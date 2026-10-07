#pragma once
#include "hive/game/detection/text.hpp"
#include "hive/game/detection/landmarks.hpp"
#include "hive/core/tasks.hpp"
#include <variant>

namespace hive::workflows {
enum class InteractionStage { idle, waiting, pressing, confirming, completed, failed, stopped };
class Interaction {
public:
    Interaction(game::TextTarget prompt,game::TextTarget result,Time timeout);
    Interaction(game::PromptLandmark prompt,game::PromptLandmark result,Time timeout);
    void start(const ObservationContext& context);
    void update(const ObservationContext& context,const game::State& state,TaskPort& tasks,ModuleScope scope);
    void stop(TaskPort& tasks,ModuleScope scope);
    InteractionStage stage() const { return stage_; }
    const std::string& reason() const { return reason_; }
private:
    using Target=std::variant<game::TextTarget,game::PromptLandmark>;
    Target prompt_,result_;
    Time timeout_,minimum_{},last_result_{Time(-1)},last_tick_{};
    ObservationContext started_;
    InteractionStage stage_{InteractionStage::idle};
    std::string pending_,reason_;
    bool prompt_seen_{};
    bool owned_{};
    game::Reading<bool> read(const Target& target,const ObservationContext& context,const game::State& state) const;
    void finish(InteractionStage stage,std::string reason,TaskPort& tasks,ModuleScope scope);
};
}
