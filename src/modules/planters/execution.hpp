#pragma once
#include "account.hpp"
#include "core/runtime/program.hpp"
#include "game/actions/travel/services.hpp"
#include "game/actions/travel/field_routes.hpp"
#include <atomic>
#include <mutex>

namespace hive::features::planters {
struct Snapshot {
    Observation<std::vector<Group>> nectar;
    Observation<Placements> placements;
    Observation<bool> harvest_prompt,growth_bar;
};
class Source {
public:
    virtual ~Source()=default;
    virtual Snapshot read(const automation::View& view) const=0;
    virtual Snapshot read_at(const automation::View& view,const Stats&) const { return read(view); }
    virtual Observation<bool> placement_visible(const automation::View&) const { return {}; }
};
class Actions {
public:
    virtual ~Actions()=default;
    virtual std::optional<automation::ApproachLandmark> locate(const Stats&) const { return {}; }
    virtual std::optional<automation::InspectInventoryItem> inspect(const Stats&) const { return {}; }
    virtual std::vector<automation::Step> prepare(Operation operation,const Stats& planter) const=0;
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
    automation::ProgramFactory route_recovery;
    movement::Source movement;
};
enum class ExecutionStage { ready, planning, preparing, travelling, checking, acting, confirming, looting, returning, checking_backpack, converting, finished, locating };
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
