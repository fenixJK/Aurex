#pragma once
#include "core/runtime/tasks.hpp"
#include "game/movement/movement.hpp"
#include "game/movement/camera.hpp"
#include <variant>

namespace hive::workflows {
enum class CheckpointStage { idle, waiting, moving, confirming, completed, failed, stopped };
enum class CheckpointFailure { none, missing, timeout, interrupted, context, input };
enum class CheckpointCompletion { observation, movement, launch };
struct CheckpointPlan {
    std::string detector;
    std::vector<Action> approach, nudge;
    Time timeout{10000}, observation_age{1000};
    std::size_t attempts{3};
    Time camera_settle{game::camera_settle(game::CameraFinish::observation)};
    std::vector<game::WalkLeg> path;
    double speed{28};
    std::vector<game::WalkLeg> nudge_path;
    CheckpointCompletion completion{CheckpointCompletion::observation};
    bool continuous{};
    std::vector<std::variant<Action,game::WalkLeg>> sequence;
    std::vector<Key> launch_movement;
    Time minimum_duration{};
    std::string observation_scope;
    std::vector<Action> launch_prepare;
    std::optional<game::CameraMove> camera;
};
void validate(const CheckpointPlan& plan);
bool uses_camera(const CheckpointPlan& plan);
class Checkpoint {
public:
    explicit Checkpoint(CheckpointPlan plan);
    void start(const ObservationContext& context,ModuleScope scope);
    void update(const ObservationContext& context,const Observation<bool>& detected,TaskPort& tasks,const Observation<double>& speed={});
    void stop(TaskPort& tasks);
    CheckpointStage stage() const { return stage_; }
    CheckpointFailure failure() const { return failure_; }
    const std::string& reason() const { return reason_; }
    const std::string& detector() const { return plan_.detector; }
    bool launched() const { return launched_; }
private:
    CheckpointPlan plan_;
    std::shared_ptr<game::MovementSpeed> speed_;
    std::shared_ptr<game::DistanceIntegrator> distance_;
    CheckpointStage stage_{CheckpointStage::idle};
    CheckpointFailure failure_{CheckpointFailure::none};
    ObservationContext started_;
    ModuleScope scope_;
    std::string pending_,reason_;
    Time last_{},minimum_{},seen_{Time(-1)},executed_{};
    std::size_t index_{},attempts_{};
    bool owned_{},nudging_{},awaiting_view_{},launched_{},observation_paused_{};
    void finish(CheckpointStage stage,std::string reason,TaskPort& tasks);
    void confirm(Time stopped);
    Time cancel_tasks(TaskPort& tasks,Time stopped={});
};
}
