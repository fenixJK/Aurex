#pragma once
#include "core/runtime/program.hpp"
#include "game/movement/field_plans.hpp"
#include "core/module_host/requests.hpp"
#include "game/actions/travel/services.hpp"
#include "game/detection/detection.hpp"
#include <mutex>

namespace hive::features::gathering {
struct FieldPlan {
    game::FieldPreference preference;
    automation::Pattern pattern;
    std::vector<automation::Step> setup;
    automation::ProgramFactory return_setup;
    std::optional<game::CameraMove> rotation;
    automation::ProgramFactory strategy;
    double sprinkler_forward{4.5},sprinkler_lateral{4.5};
    Point start_offset;
    bool reset_return{};
    std::optional<bool> shift_lock;
    std::function<std::vector<automation::Step>()> prepare;
    std::vector<std::string> extend_while;
    Time extension_limit{std::chrono::minutes(5)};
};
struct Snapshot {
    game::FieldContext field;
    std::vector<game::FieldGoal> goals;
};
class Source {
public:
    virtual ~Source()=default;
    virtual Snapshot read(const ObservationContext& context,const game::State& observations) const=0;
    virtual std::vector<game::FieldGoal> goals(const ObservationContext& context,const game::State& observations) const { return read(context,observations).goals; }
};
class NavigationSource final : public Source {
    std::shared_ptr<const automation::NavigationPerception> perception_;
    std::shared_ptr<const game::FieldGoalSource> goals_;
public:
    NavigationSource(std::shared_ptr<const automation::NavigationPerception> perception,std::shared_ptr<const game::FieldGoalSource> goals);
    Snapshot read(const ObservationContext& context,const game::State& observations) const override;
    std::vector<game::FieldGoal> goals(const ObservationContext& context,const game::State&) const override;
};
struct Plan {
    std::vector<FieldPlan> fields;
    std::string hive;
    std::size_t cycles{1};
    automation::RunConfig config{Time(3600000),28,{},true};
    automation::ProgramFactory hive_setup;
    automation::ProgramFactory recovery;
    std::size_t max_recoveries{2};
    automation::ProgramFactory route_recovery;
    bool shift_lock{};
    std::shared_ptr<const game::DetectionSource> buffs,field_boosts;
    std::function<std::unique_ptr<automation::Program>(unsigned)> whirligig_return;
};
enum class Phase { waiting, travelling, preparing, gathering, returning, converting, finished, recovering };
struct Report {
    automation::State state{automation::State::ready};
    Phase phase{Phase::waiting};
    std::optional<std::string> field;
    game::FieldReason reason{game::FieldReason::no_field};
    std::vector<game::FieldGoal> contributors;
    std::size_t completed_cycles{},target_cycles{};
    std::optional<Time> converted_at;
    std::size_t recoveries{};
};
class Gathering;
class Results {
    friend class Gathering;
    mutable std::mutex mutex_;
    Report report_;
public:
    Report read() const;
};
struct Run { std::unique_ptr<automation::Program> program; std::shared_ptr<const Results> results; };
Run run(Plan plan,std::shared_ptr<const game::RouteNetwork> network,
    std::shared_ptr<const game::FieldPolicy> policy,std::shared_ptr<const Source> source);
std::unique_ptr<automation::Program> program(Plan plan,std::shared_ptr<const game::RouteNetwork> network,
    std::shared_ptr<const game::FieldPolicy> policy,std::shared_ptr<const Source> source);
}
