#pragma once
#include "controller.hpp"
#include "planning.hpp"
#include "preferences.hpp"
#include "hive/automation/program.hpp"
#include "hive/automation/navigation.hpp"
#include "hive/automation/field_routes.hpp"
#include "hive/game/detection/nectar.hpp"
#include "hive/game/detection/planters.hpp"
#include <atomic>
#include <mutex>

namespace hive::features::planters {
struct Snapshot {
    Observation<std::vector<Group>> nectar;
    Observation<Placements> placements;
    Observation<Growth> growth;
    Observation<game::NectarLevels> nectar_levels;
    Observation<std::string> harvest_prompt;
    Observation<std::vector<game::PlanterReward>> rewards;
};
class Source {
public:
    virtual ~Source()=default;
    virtual Snapshot read(const automation::View& view) const=0;
    virtual Snapshot read_at(const automation::View& view,const std::string&) const { return read(view); }
};
class Actions {
public:
    virtual ~Actions()=default;
    virtual std::optional<automation::ApproachLandmark> locate(const Stats&) const { return {}; }
    virtual std::optional<automation::InspectInventoryItem> inspect(const Stats&) const { return {}; }
    virtual std::vector<automation::Step> prepare(Operation operation,const Stats& planter) const=0;
};
class Execution;
class Account {
    friend class Execution;
    std::shared_ptr<Store> store_;
    std::atomic_bool busy_{};
    std::atomic_uint64_t edit_revision_{};
    AdaptivePlanner planner_;
    TravelTimes travel_;
    std::array<std::optional<Phase>,5> nectar_phases_;
    std::array<std::pair<std::uint64_t,std::string>,5> last_fields_;
    mutable std::mutex snapshot_mutex_;
    std::vector<Plant> plants_;
public:
    explicit Account(std::shared_ptr<Store> store);
    std::vector<Plant> plants() const;
    Ledger saved() const;
    bool busy() const { return busy_.load(); }
    std::uint64_t edit_revision() const { return edit_revision_.load(); }
    void remove_saved(std::uint64_t id);
    void set_saved(std::optional<std::uint64_t> id,const Stats& stats,double progress,WallTime now);
};
struct FieldVisit {
    automation::ProgramFactory departure;
    std::vector<automation::Walk> loot;
    automation::ProgramFactory walking_departure;
};
struct HarvestConversion { double threshold{95}; bool enabled{true}; bool every_return{}; };
struct ExecutionPlan {
    std::map<std::string,std::string> destinations;
    std::size_t capacity{3},max_actions{6},preview_steps{4096};
    automation::RunConfig config{Time(3600000),28,{},true,true};
    std::optional<Preferences> preferences;
    automation::ProgramFactory setup;
    std::optional<std::string> return_to;
    std::map<std::string,FieldVisit> visits;
    bool collect_loot{true};
    bool attempt_unscanned_placement{};
    std::optional<HarvestConversion> conversion;
    bool walk_return{};
    automation::ProgramFactory death_recovery;
    movement::Source movement;
};
enum class ExecutionStage { ready, planning, preparing, travelling, checking, acting, confirming, reconciling, looting, returning, checking_backpack, converting, finished, locating };
struct ExecutionReport {
    automation::State state{automation::State::ready};
    ExecutionStage stage{ExecutionStage::ready};
    std::size_t confirmed{},growing{};
    std::optional<Intent> pending;
    std::string field,planter;
    std::vector<std::pair<std::uint64_t,std::string>> records;
    std::size_t placements{},harvests{};
};
class ExecutionResults {
    friend class Execution;
    mutable std::mutex mutex_;
    ExecutionReport report_;
public:
    ExecutionReport read() const;
};
using WallClock=std::function<WallTime()>;
struct ExecutionRun { std::unique_ptr<automation::Program> program; std::shared_ptr<const ExecutionResults> results; };
ExecutionRun program(ExecutionPlan plan,std::shared_ptr<const game::RouteNetwork> network,std::shared_ptr<Account> account,
    std::shared_ptr<const Source> source,std::shared_ptr<const Actions> actions,WallClock clock);
ExecutionPlan walking_plan(const workflows::RouteCatalog& routes,double speed,automation::ProgramFactory return_to_hive,
    automation::FieldReturnOptions returns={});
}
