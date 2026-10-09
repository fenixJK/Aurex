#pragma once
#include "game/detection/state.hpp"
#include "core/runtime/tasks.hpp"

namespace hive::workflows {
enum class ConversionStage { idle, waiting, confirming, converting, paused, completed, recovery_required, stopped };
struct ConversionConfig {
    Time reading_age{2000}, observation_timeout{10000}, confirmation_timeout{5000}, total_timeout{300000};
    int attempts{3};
    bool balloon{};
};
class Conversion {
public:
    explicit Conversion(ConversionConfig config={});
    void start(const game::Context& context);
    void update(const game::Context& context,const game::State& state,TaskPort& tasks,ModuleScope scope);
    void stop(TaskPort& tasks,ModuleScope scope);
    void pause(const game::Context& context,TaskPort& tasks,ModuleScope scope);
    void resume(const game::Context& context);
    ConversionStage stage() const { return stage_; }
    const std::string& reason() const { return reason_; }
private:
    ConversionConfig config_;
    ConversionStage stage_{ConversionStage::idle};
    game::Context started_;
    Time last_tick_{},last_evidence_{},last_attempt_{},minimum_capture_{};
    std::string pending_,reason_;
    int attempts_{};
    bool control_{},confirmed_hive_{};
    Time paused_at_{};
    void finish(ConversionStage stage,std::string reason,TaskPort& tasks,ModuleScope scope);
};
}
