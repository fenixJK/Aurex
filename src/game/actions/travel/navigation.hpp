#pragma once
#include "checkpoint.hpp"
#include "travel.hpp"
#include "game/detection/state.hpp"
#include <functional>
#include <memory>

namespace hive::workflows {
struct ExecutableRoute {
    game::Route route;
    std::vector<CheckpointPlan> checkpoints;
};
class RouteCatalog {
public:
    RouteCatalog(std::vector<std::string> anchors,std::vector<ExecutableRoute> routes);
    RouteCatalog(const RouteCatalog& initial,std::function<std::shared_ptr<const RouteCatalog>()> refresh);
    std::shared_ptr<const RouteCatalog> refreshed() const;
    const game::RouteNetwork& network() const;
    const std::vector<CheckpointPlan>& checkpoints(const std::string& route) const;
private:
    std::function<std::shared_ptr<const RouteCatalog>()> refresh_;
    struct Data;
    std::shared_ptr<const Data> data_;
};
struct NavigationObservations {
    Observation<std::string> anchor;
    std::map<std::string,Observation<bool>> checkpoints;
    game::Reading<bool> alive;
    game::Reading<game::Connection> connection;
    game::Reading<double> movement_speed;
    bool conflicting_anchors{};
};
struct NavigationStatus {
    TravelStage stage{TravelStage::idle};
    std::string route,checkpoint,reason;
    TravelFailure failure{TravelFailure::none};
};
class Navigation final : private TaskPort {
public:
    Navigation(std::shared_ptr<const RouteCatalog> catalog,TaskPort& tasks,ModuleScope scope,TravelLimits limits={});
    ~Navigation();
    Navigation(const Navigation&)=delete;
    Navigation& operator=(const Navigation&)=delete;
    void start(std::string destination,const ObservationContext& context,std::optional<TravelPosition> departure={});
    void update(const ObservationContext& context,const NavigationObservations& observations,
        const std::set<std::string>& available={},const std::set<std::string>& blocked={});
    void stop();
    const NavigationStatus& status() const { return status_; }
    const std::set<std::string>& failed_routes() const { return travel_.failed_routes(); }
    const std::optional<TravelPosition>& position() const { return travel_.position(); }
    const std::optional<game::RouteExecution>& execution() const { return execution_; }
private:
    std::shared_ptr<const RouteCatalog> catalog_;
    TaskPort& tasks_;
    ModuleScope scope_;
    Travel travel_;
    NavigationStatus status_;
    std::optional<TravelLeg> leg_;
    std::optional<Checkpoint> checkpoint_;
    ObservationContext started_;
    std::optional<TravelPosition> departure_;
    std::optional<game::RouteExecution> execution_;
    Time minimum_{};
    std::optional<Time> landing_after_;
    std::size_t next_checkpoint_{};
    bool owned_{},launched_{};
    bool active() const;
    void finish(TravelStage stage,std::string reason,TravelFailure failure=TravelFailure::none);
    void check_scope(ModuleScope scope) const;
    bool acquire(ModuleScope scope) override;
    void release(ModuleScope scope) override;
    std::string submit(ModuleScope scope,TaskClass kind,std::vector<Action> actions,Time lifetime) override;
    std::vector<TaskResult> results(ModuleScope scope) override;
    void cancel(ModuleScope scope) override;
};
}
