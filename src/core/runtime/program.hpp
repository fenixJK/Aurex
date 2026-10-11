#pragma once
#include "game/movement/movement.hpp"
#include "game/movement/navigation.hpp"
#include "game/movement/camera.hpp"
#include "settings.hpp"
#include "statistics.hpp"
#include "activity_status.hpp"
#include "game/detection/text.hpp"
#include "game/detection/landmarks.hpp"
#include "game/detection/localization.hpp"
#include "game/detection/state.hpp"
#include "game/actions/travel/checkpoint.hpp"
#include "game/actions/field/sprinkler_placement.hpp"
#include "game/actions/inventory/inventory.hpp"
#include "game/actions/interaction/dialog.hpp"
#include <memory>
#include <variant>
#include <set>
#include <functional>

namespace hive::automation {
using Value = SettingValue;
using Settings = hive::Settings;
enum class Permission { movement, tool, interact };
using Permissions = std::set<Permission>;
class ClientRejoinRequired : public std::runtime_error { public: using std::runtime_error::runtime_error; };
enum class State { ready, running, paused, completed, stopped, failed, deferred };
inline bool can_handoff(State state) { return state==State::completed || state==State::deferred; }
class ObservationUnavailable : public std::runtime_error { public: using std::runtime_error::runtime_error; };
enum class Interruption { death, lost_route, disconnected, lost_observations, client_rejoin };
struct SharedActivity { std::string service; Settings target; };
struct Progress { State state{State::ready}; Time elapsed{}; std::size_t steps{}; std::string message; std::optional<double> backpack_percent; std::string module_id; std::optional<unsigned> estimated_heading; std::optional<game::RoutePosition> route_position; std::optional<game::RouteExecution> route_execution; std::optional<Interruption> interruption; WorkActivity activity{WorkActivity::other}; std::string activity_label; bool input_released{true}; ActivityStatus status; std::string field; std::optional<SharedActivity> shared_activity; };
inline bool can_handoff(const Progress& progress) {
    return progress.input_released && (can_handoff(progress.state) || (progress.state==State::failed
        && progress.interruption!=Interruption::disconnected && progress.interruption!=Interruption::client_rejoin));
}
struct RunConfig { Time duration{60000}; double speed{28}; std::optional<double> backpack_limit; bool observe{}, require_done{}; };
struct View {
    Time elapsed{};
    std::size_t completed_steps{};
    std::optional<double> backpack_percent;
    ObservationContext context;
    const game::State* observations{};
    std::optional<unsigned> estimated_heading;
    std::optional<game::RoutePosition> route_position;
    std::optional<game::RouteExecution> route_execution;
    const Progress* completed{};
};
enum class GuardState { ready, waiting, completed, failed };
struct GuardDecision { GuardState state{GuardState::ready}; std::string message; std::optional<Interruption> interruption; };
class RunGuard {
public:
    virtual ~RunGuard()=default;
    virtual std::vector<Rect> text_regions() const { return {}; }
    virtual GuardDecision evaluate(const View& view,Time started)=0;
};
struct Walk { game::WalkLeg leg; bool tool{}; };
struct MoveCamera { game::CameraMove movement; };
struct SetHeading { unsigned heading{}; };
struct AdoptHeading { unsigned heading{}; Observation<bool> reference; };
struct ObserveHeading { unsigned heading{}; std::shared_ptr<const game::LandmarkSource> reference; Time timeout{3000}; };
struct Hold { std::vector<Key> keys; Time duration{}; std::optional<Point> pointer; };
struct Done { std::string reason; };
struct Defer { std::string reason; };
struct EndCycle {};
struct ReportActivity { ActivityStatus status; std::optional<Rect> image_region; };
struct Wait { Time duration{50}; std::string reason{"Waiting for observations"}; };
struct AnswerDialog { workflows::DialogPlan plan; };
struct UseControl { Observation<Point> target; int scroll{}; std::string label; std::optional<Point> rest; };
struct TapMenuKey { Key key; std::string label; };
struct EnterNumber { Observation<Point> target; unsigned value{}; };
struct AwaitText { game::TextTarget target; Time timeout{10000}; };
struct UseStation { std::string name; Time timeout{15000}; };
struct UsePrompt { game::PromptLandmark prompt,result; Time timeout{15000}; };
struct Convert { bool balloon{},after_gather{}; };
struct PlaceSprinkler { workflows::SprinklerPlan plan; std::shared_ptr<const game::SprinklerSource> source; };
struct DragInventoryItem {
    workflows::InventoryDragPlan plan;
    std::shared_ptr<const game::InventorySource> source;
    std::shared_ptr<Observation<workflows::InventoryMatch>> result;
};
struct InspectInventoryItem {
    std::vector<std::string> names;
    std::shared_ptr<const game::InventorySource> source;
    std::shared_ptr<Observation<workflows::InventoryMatch>> result;
    bool current_page_only{};
    bool start_at_bottom{};
    bool quantity{};
};
struct Navigate { std::string destination; std::optional<InspectInventoryItem> inventory_search; };
class Program;
using ProgramFactory=std::function<std::unique_ptr<Program>()>;
struct RunProgram { ProgramFactory create; bool return_result{}; };
struct FollowPath { workflows::CheckpointPlan movement; };
struct ApproachLandmark { workflows::CheckpointPlan movement; std::shared_ptr<const game::LandmarkSource> source; };
struct ApproachPrompt { workflows::CheckpointPlan movement; game::PromptLandmark prompt; };
struct Pattern {
    std::vector<Walk> legs;
    Time duration{60000};
    std::optional<double> backpack_limit;
    std::optional<game::TextTarget> until;
    std::string label{"Following pattern"};
    std::shared_ptr<const game::LandmarkSource> outcome;
    std::optional<Observation<bool>> outcome_baseline;
    ProgramFactory between_cycles;
    std::function<bool()> yield;
    WorkActivity activity{WorkActivity::other};
    std::function<unsigned(const View&)> hotbar;
};
using Step = std::variant<Walk,Hold,MoveCamera,SetHeading,AdoptHeading,ObserveHeading,Done,Defer,Wait,UseControl,AwaitText,UseStation,UsePrompt,Convert,Pattern,ApproachPrompt,ApproachLandmark,Navigate,PlaceSprinkler,DragInventoryItem,RunProgram,FollowPath,AnswerDialog,InspectInventoryItem,EndCycle,EnterNumber,TapMenuKey,ReportActivity>;
class Program {
public:
    virtual ~Program() = default;
    virtual RunConfig config() const = 0;
    virtual std::vector<Rect> text_regions() const { return {}; }
    virtual WorkActivity activity() const noexcept { return WorkActivity::other; }
    virtual std::string field() const { return {}; }
    virtual std::optional<SharedActivity> shared_activity() const { return {}; }
    virtual std::optional<game::MenuTab> movement_menu() const { return {}; }
    virtual std::string activity_label() const { return {}; }
    virtual ActivityStatus activity_status() const { return {activity_label(),{}}; }
    virtual std::unique_ptr<RunGuard> guard() const { return {}; }
    virtual Step next(const View& view) = 0;
    virtual bool expects_respawn() const noexcept { return false; }
    virtual bool expects_reconnect() const noexcept { return false; }
    virtual ProgramFactory recovery(Interruption,const View&) { return {}; }
    virtual bool resume_after_reconnect(const View&) { return false; }
    virtual bool request_yield() { return false; }
    virtual void observe(const View&) {}
    virtual void step_completed(const View&) {}
    virtual void finished(State) {}
};
struct Launch {
    std::string module_id;
    Permissions permissions;
    std::unique_ptr<Program> program;
};
}
