#pragma once
#include "core/runtime/program.hpp"
#include "game/movement/navigation.hpp"
#include <mutex>

namespace hive::automation {
class AvailabilitySource {
public:
    virtual ~AvailabilitySource()=default;
    virtual std::vector<Rect> text_regions(const std::string&) const { return {}; }
    virtual Observation<bool> read(const std::string& id,const View& view) const=0;
    virtual Observation<Time> remaining(const std::string&,const View&) const { return {}; }
};
std::shared_ptr<const AvailabilitySource> station_availability(std::map<std::string,std::string> stations);
enum class VisitStage { pending, deferred, unavailable, preparing, travelling, working, awaiting_loot, looting, settling, completed, interrupted };
struct VisitResult {
    std::string id;
    VisitStage stage{VisitStage::pending};
    std::optional<Time> confirmed_at,estimated_ready_at,observed_ready_at;
};
struct VisitReport { State state{State::ready}; std::vector<VisitResult> visits; std::uint64_t generation{}; Window window; };
class VisitProgram;
class VisitResults {
    friend class VisitProgram;
    mutable std::mutex mutex_;
    VisitReport report_;
public:
    VisitReport read() const;
};
struct Visit {
    std::string id,destination;
    std::variant<UseStation,Pattern> action;
    std::vector<Walk> loot;
    Time cooldown{};
    bool inspect_when_unknown{};
    ProgramFactory departure;
    std::shared_ptr<const game::LandmarkSource> approach_scope;
    std::string approach_location;
    std::string group;
    Time settle{};
    std::function<std::unique_ptr<Program>(Time)> action_program;
    std::string working_status;
    std::function<bool(const ObservationContext&)> scheduled;
    std::function<std::optional<Time>()> retry_after_completion;
    Time loot_duration{};
    ProgramFactory approach;
    std::function<Time()> cooldown_after_completion;
};
struct VisitPlan {
    std::vector<Visit> visits;
    std::optional<std::string> finish_at;
    RunConfig config{Time(3600000),28,{},true,true};
    ProgramFactory setup;
    ProgramFactory recovery;
    std::function<void(std::vector<Visit>&)> prepare;
};
struct VisitRun { std::unique_ptr<Program> program; std::shared_ptr<const VisitResults> results; };
VisitRun visit_program(VisitPlan plan,std::shared_ptr<const game::RouteNetwork> network,
    std::shared_ptr<const AvailabilitySource> availability);
}
