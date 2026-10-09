#include "navigation.hpp"

namespace hive::workflows {
namespace {
std::vector<game::Route> edges(const std::vector<ExecutableRoute>& routes) {
    if (routes.size()>1024) throw std::invalid_argument("Too many executable routes");
    std::vector<game::Route> result;
    for (const auto& route : routes) result.push_back(route.route);
    return result;
}
std::shared_ptr<const RouteCatalog> refreshed(std::shared_ptr<const RouteCatalog> catalog) {
    if (catalog) if (auto current=catalog->refreshed()) return current;
    return catalog;
}
const RouteCatalog& require_catalog(const std::shared_ptr<const RouteCatalog>& catalog) {
    if (!catalog) throw std::invalid_argument("Navigation needs a route catalog");
    return *catalog;
}
}
struct RouteCatalog::Data {
    game::RouteNetwork network;
    std::map<std::string,std::vector<CheckpointPlan>> checkpoints;
    Data(std::vector<std::string> anchors,const std::vector<ExecutableRoute>& routes):network(std::move(anchors),edges(routes)) {}
};
RouteCatalog::RouteCatalog(std::vector<std::string> anchors,std::vector<ExecutableRoute> routes) {
    auto data=std::make_shared<Data>(std::move(anchors),routes);
    std::size_t actions=0;
    for (auto& route : routes) {
        if (route.checkpoints.empty() || route.checkpoints.size()>64) throw std::invalid_argument("Route needs 1–64 checkpoints");
        for (const auto& checkpoint : route.checkpoints) {
            validate(checkpoint);
            if (!checkpoint.observation_scope.empty() && !data->network.contains(checkpoint.observation_scope))
                throw std::invalid_argument("Route observation scope is not in the location catalog");
            actions+=checkpoint.approach.size()+checkpoint.nudge.size()+checkpoint.path.size()+checkpoint.nudge_path.size()+checkpoint.sequence.size();
        }
        if (actions>65536) throw std::invalid_argument("Route catalog exceeds execution limits");
        data->checkpoints.emplace(route.route.id,std::move(route.checkpoints));
    }
    data_=std::move(data);
}
RouteCatalog::RouteCatalog(const RouteCatalog& initial,std::function<std::shared_ptr<const RouteCatalog>()> refresh)
    :refresh_(std::move(refresh)),data_(initial.data_) {}
std::shared_ptr<const RouteCatalog> RouteCatalog::refreshed() const { return refresh_?refresh_():nullptr; }
const game::RouteNetwork& RouteCatalog::network() const { return data_->network; }
const std::vector<CheckpointPlan>& RouteCatalog::checkpoints(const std::string& route) const { return data_->checkpoints.at(route); }
Navigation::Navigation(std::shared_ptr<const RouteCatalog> catalog,TaskPort& tasks,ModuleScope scope,TravelLimits limits)
    :catalog_(refreshed(std::move(catalog))),tasks_(tasks),scope_(scope),travel_(require_catalog(catalog_).network(),limits) {}
Navigation::~Navigation() { if (active() || owned_) finish(TravelStage::stopped,"Navigation stopped"); }
bool Navigation::active() const {
    return status_.stage==TravelStage::locating || status_.stage==TravelStage::travelling || status_.stage==TravelStage::confirming;
}
void Navigation::start(std::string destination,const ObservationContext& c,std::optional<TravelPosition> departure) {
    if (status_.stage!=TravelStage::idle || !scope_.id || scope_.generation!=c.generation)
        throw std::logic_error("Navigation needs a new session-bound request");
    travel_.start(std::move(destination),c); started_=c; minimum_=c.now; departure_=std::move(departure);
    status_={TravelStage::locating,{},{},"Locating departure anchor"};
}
void Navigation::finish(TravelStage stage,std::string reason,TravelFailure failure) {
    status_.failure=failure;
    execution_.reset();
    if (stage!=TravelStage::arrived) travel_.stop();
    status_.stage=stage; status_.reason=std::move(reason);
    if (owned_) {
        try {
            tasks_.cancel(scope_);
            bool faulted=false;
            for (const auto& result : tasks_.results(scope_)) faulted|=result.outcome==TaskOutcome::faulted;
            tasks_.release(scope_);
            if (faulted) throw std::runtime_error("Navigation input was interrupted");
        }
        catch (const std::exception& e) { status_.stage=TravelStage::failed; status_.failure=TravelFailure::input; status_.reason+="; "+std::string(e.what()); }
        catch (...) { status_.stage=TravelStage::failed; status_.failure=TravelFailure::input; status_.reason="Could not release navigation input"; }
    }
    if (status_.stage!=TravelStage::arrived) travel_.stop();
    owned_=false; checkpoint_.reset(); leg_.reset(); departure_.reset();
}
void Navigation::stop() { if (active()) finish(TravelStage::stopped,"Navigation stopped"); }
void Navigation::check_scope(ModuleScope scope) const {
    if (!owned_ || scope!=scope_) throw std::logic_error("Checkpoint does not own navigation control");
}
bool Navigation::acquire(ModuleScope scope) { check_scope(scope); return true; }
void Navigation::release(ModuleScope scope) { check_scope(scope); }
std::string Navigation::submit(ModuleScope scope,TaskClass kind,std::vector<Action> actions,Time lifetime) {
    check_scope(scope);
    if (kind!=TaskClass::travel) throw std::invalid_argument("Navigation can only submit travel tasks");
    return tasks_.submit(scope,kind,std::move(actions),lifetime);
}
std::vector<TaskResult> Navigation::results(ModuleScope scope) { check_scope(scope); return tasks_.results(scope); }
void Navigation::cancel(ModuleScope scope) { check_scope(scope); tasks_.cancel(scope); }
void Navigation::update(const ObservationContext& c,const NavigationObservations& observations,
    const std::set<std::string>& available,const std::set<std::string>& blocked) {
    if (!active()) return;
    try {
        if (observations.conflicting_anchors) { finish(TravelStage::failed,"Conflicting location observations",TravelFailure::conflict); return; }
        if ((observations.alive.has_value(c,Time(2000),started_.now) && !observations.alive.value)
            || (observations.connection.has_value(c,Time(2000),started_.now) && observations.connection.value==game::Connection::disconnected)) {
            finish(TravelStage::failed,"Game disconnected or player respawning",TravelFailure::interrupted); return;
        }
        if (!owned_) {
            if (tasks_.acquire(scope_)) {
                owned_=true; minimum_=departure_ && departure_->usable(c)?departure_->context.now:c.now;
            }
            else departure_.reset();
        }
        auto anchor=observations.anchor;
        if (!owned_ || anchor.captured<minimum_) anchor={};
        if (landing_after_ && anchor.has_value(c,Time(2000),*landing_after_) && anchor.captured>*landing_after_
            && anchor.value!=catalog_->network().route(status_.route).to) {
            finish(TravelStage::failed,"Transport landed away from its destination",TravelFailure::location); return;
        }
        auto dispatched=travel_.update(c,anchor,available,blocked,departure_);
        if (travel_.stage()!=TravelStage::locating) departure_.reset();
        status_.stage=travel_.stage();
        if (status_.stage==TravelStage::failed || status_.stage==TravelStage::arrived) {
            finish(status_.stage,travel_.reason(),travel_.failure()); return;
        }
        if (!owned_) { status_.reason="Waiting for navigation control"; return; }
        if (dispatched) {
            execution_.reset();
            launched_=false; landing_after_.reset();
            leg_=std::move(dispatched); next_checkpoint_=0;
            status_.route=leg_->route.id; status_.checkpoint.clear();
        }
        if (execution_) execution_->context=c;
        if (!leg_) return;
        const auto& plans=catalog_->checkpoints(leg_->route.id);
        if (!checkpoint_) {
            checkpoint_.emplace(plans.at(next_checkpoint_)); checkpoint_->start(c,scope_);
            status_.checkpoint=checkpoint_->detector();
            execution_=game::RouteExecution{leg_->route.id,leg_->route.from,leg_->route.to,next_checkpoint_,c.now,c,plans.at(next_checkpoint_).observation_scope};
        }
        execution_->context=c;
        const auto found=observations.checkpoints.find(checkpoint_->detector());
        checkpoint_->update(c,found==observations.checkpoints.end()?Observation<bool>{}:found->second,*this,observations.movement_speed);
        if (plans.at(next_checkpoint_).completion==CheckpointCompletion::launch
            && (checkpoint_->stage()==CheckpointStage::moving || checkpoint_->stage()==CheckpointStage::confirming || checkpoint_->stage()==CheckpointStage::completed))
            launched_=true;
        status_.reason="Following "+status_.route+(status_.checkpoint.empty()?"":": "+status_.checkpoint);
        if (checkpoint_->stage()==CheckpointStage::failed) {
            const auto failure=checkpoint_->failure();
            if (launched_ || (failure!=CheckpointFailure::missing && failure!=CheckpointFailure::timeout)) {
                finish(TravelStage::failed,checkpoint_->reason(),failure==CheckpointFailure::context?TravelFailure::context:
                    failure==CheckpointFailure::input?TravelFailure::input:failure==CheckpointFailure::missing?TravelFailure::location:
                    failure==CheckpointFailure::timeout?TravelFailure::timeout:TravelFailure::interrupted); return;
            }
            const auto reason=checkpoint_->reason();
            if (!travel_.finished(leg_->ticket,c,false)) { finish(TravelStage::failed,travel_.reason(),travel_.failure()); return; }
            checkpoint_.reset(); leg_.reset(); status_.stage=TravelStage::confirming;
            execution_.reset();
            status_.reason=reason+"; locating before replanning";
        } else if (checkpoint_->stage()==CheckpointStage::completed) {
            Observation<std::string> arrival;
            if (plans.at(next_checkpoint_).completion==CheckpointCompletion::observation && checkpoint_->detector()==leg_->route.to
                && found!=observations.checkpoints.end() && anchor.captured==found->second.captured)
                arrival=anchor;
            checkpoint_.reset();
            if (++next_checkpoint_==plans.size()) {
                if (!travel_.finished(leg_->ticket,c,true,arrival)) { finish(TravelStage::failed,travel_.reason(),travel_.failure()); return; }
                if (launched_ && leg_->route.arrival==game::RouteArrival::observed) landing_after_=c.now;
                status_.reason=leg_->route.arrival==game::RouteArrival::estimated?"Finishing route":"Confirming arrival anchor";
                leg_.reset(); status_.stage=TravelStage::confirming;
            }
        }
    } catch (const std::exception& e) { finish(TravelStage::failed,e.what(),TravelFailure::input); }
    catch (...) { finish(TravelStage::failed,"Navigation failed unexpectedly",TravelFailure::input); }
}
}
