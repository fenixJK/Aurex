#pragma once
#include "hive/game/detection/sprinklers.hpp"
#include "hive/core/tasks.hpp"

namespace hive::workflows {
enum class SprinklerCompletion { observed, attempted };
struct SprinklerPlan {
    std::string field,site;
    unsigned slot{1};
    Time jump{},timeout{10000};
    SprinklerCompletion completion{SprinklerCompletion::observed};
};
void validate(const SprinklerPlan& plan);
enum class SprinklerStage { idle, waiting, placing, confirming, completed, failed, stopped };
class Sprinkler {
    SprinklerPlan plan_;
    ObservationContext started_;
    SprinklerStage stage_{SprinklerStage::idle};
    std::string pending_,reason_;
    Time last_tick_{},minimum_{},last_frame_{Time(-1)};
    bool attempted_{};
    unsigned attempts_{};
    Time input_finished_{};
    void dispatch(TaskPort& tasks,ModuleScope scope);
    void update_attempt(const ObservationContext& context,const game::State& state,TaskPort& tasks,ModuleScope scope);
    void finish(SprinklerStage stage,std::string reason,TaskPort& tasks,ModuleScope scope);
public:
    explicit Sprinkler(SprinklerPlan plan);
    void start(const ObservationContext& context);
    void update(const ObservationContext& context,const game::State& state,const Observation<game::SprinklerSite>& site,TaskPort& tasks,ModuleScope scope);
    void stop(TaskPort& tasks,ModuleScope scope);
    SprinklerStage stage() const { return stage_; }
    const std::string& reason() const { return reason_; }
    bool attempted() const { return attempted_; }
};
}
