#include "field_routes.hpp"
#include "game/actions/hive/hive_setup.hpp"
#include "game/actions/field/shift_lock.hpp"
#include "game/data/fields.hpp"
#include "route_helpers.hpp"

namespace hive::automation {
namespace {
class HiveNavigation final : public NavigationPerception {
    std::shared_ptr<const NavigationPerception> perception_;
    std::shared_ptr<HiveSetupResult> setup_;
public:
    HiveNavigation(std::shared_ptr<const NavigationPerception> perception,std::shared_ptr<HiveSetupResult> setup)
        :perception_(std::move(perception)),setup_(std::move(setup)) {
        if (!perception_ || !setup_) throw std::invalid_argument("Hive navigation requires observations and setup state");
    }
    void input_submitted(const std::vector<Action>& actions) const override {
        perception_->input_submitted(actions);
        for (const auto& action:actions) {
            for (const auto key:action.held)
                if (key<=Key::jump || camera_key(key) || key==Key::shift_lock) {
                    setup_->invalidate_camera(); return;
                }
            if (action.tap==Key::respawn || action.tap==Key::confirm) {
                setup_->invalidate_camera(); return;
            }
        }
    }
    NavigationView read(const ObservationContext& c,const game::State& state) const override {
        auto view=perception_->read(c,state);
        view.available.erase("hive.camera.prepared");
        for (unsigned slot=1;slot<=6;++slot) view.available.erase("hive.slot."+std::to_string(slot));
        if (view.observations.conflicting_anchors) return view;
        const auto setup=setup_->read();
        if (!setup || !setup->claimed_slot || setup->context.generation!=c.generation || setup->context.window.id!=c.window.id
            || setup->context.window.process!=c.window.process || setup->context.window.bounds!=c.window.bounds
            || setup->context.now>c.now || !c.generation || !c.window.id || !c.window.bounds.valid()
            || (state.alive.has_value(c,Time(2000)) && !state.alive.value)
            || (state.connection.has_value(c,Time(2000)) && state.connection.value==game::Connection::disconnected)) return view;
        view.available.insert("hive.slot."+std::to_string(*setup->claimed_slot));
        if (!setup->camera_prepared) return view;
        auto& anchor=view.observations.anchor;
        if (!anchor.has_value(c,Time(2000))) {
            const auto found=view.observations.checkpoints.find("hive.proximity");
            auto seen=found==view.observations.checkpoints.end()?Observation<bool>{}:found->second;
            if (state.at_hive.usable(c,Time(2000)) && (!seen.usable(c,Time(2000)) || state.at_hive.captured>=seen.captured))
                seen=state.at_hive;
            if (seen.has_value(c,Time(2000)) && seen.value)
                anchor={Knowledge::present,"hive.proximity",seen.captured,seen.window,seen.generation};
        }
        if (anchor.has_value(c,Time(2000)) && anchor.value=="hive.proximity") view.available.insert("hive.camera.prepared");
        return view;
    }
};
using detail::estimate;
std::vector<workflows::CheckpointPlan> return_path(const std::string& field,unsigned slot,double speed,
    const std::shared_ptr<const movement::Recipes>& recipes) {
    return recipes->path("return-route",{{"field",field},{"slot",int(slot)},{"bees",50},{"cannon",false},{"teleporter",false},{"planting",false}},speed);
}

}
std::shared_ptr<const workflows::RouteCatalog> sunflower_routes(unsigned hive_slot,double speed,std::shared_ptr<const movement::Recipes> recipes) {
    if (hive_slot<1 || hive_slot>6 || !std::isfinite(speed) || speed<8 || speed>200)
        throw std::invalid_argument("Field routes require a known hive slot and movement speed");
    if (!recipes) recipes=movement::snapshot();
    auto outbound=recipes->path("fields",{{"field",std::string("sunflower")},{"slot",int(hive_slot)}},speed);
    auto inbound=return_path("sunflower",hive_slot,speed,recipes);
    const auto outward_time=estimate(outbound),inward_time=estimate(inbound);
    return std::make_shared<workflows::RouteCatalog>(
        std::vector<std::string>{"hive.departure","sunflower.entry","hive.proximity"},
        std::vector<workflows::ExecutableRoute>{
            {{"hive-sunflower","hive.departure","sunflower.entry",outward_time,{"hive.departure.prepared"},game::RouteArrival::estimated},std::move(outbound)},
            {{"sunflower-hive","sunflower.entry","hive.proximity",inward_time,{"sunflower.return.prepared"}},std::move(inbound)}});
}
std::shared_ptr<const workflows::RouteCatalog> walking_field_routes(unsigned hive_slot,double speed,std::shared_ptr<const movement::Recipes> recipes) {
    if (!recipes) recipes=movement::snapshot();
    const auto sunflower=sunflower_routes(hive_slot,speed,recipes);
    std::vector<workflows::ExecutableRoute> routes;
    for (const auto* name : {"hive-sunflower","sunflower-hive"})
        routes.push_back({sunflower->network().route(name),sunflower->checkpoints(name)});
    const auto add=[&](const std::string& field) {
        auto outbound=recipes->path("fields",{{"field",field},{"slot",int(hive_slot)}},speed);
        auto inbound=return_path(field,hive_slot,speed,recipes);
        const auto timeout=[](const auto& path) { Time total{1500}; for (const auto& leg : path) total+=leg.timeout; return total; };
        routes.push_back({{"hive-"+field,"hive.departure",field+".entry",estimate(outbound),{"hive.departure.prepared"},game::RouteArrival::estimated,{},timeout(outbound)},std::move(outbound)});
        routes.push_back({{field+"-hive",field+".entry","hive.proximity",estimate(inbound),{field+".return.prepared"},game::RouteArrival::observed,{},timeout(inbound)},std::move(inbound)});
    };
    for (const auto* field:{"dandelion","mushroom","clover","spider","strawberry","cactus","pumpkin","pinetree","rose","pineapple","mountaintop","stump","coconut","pepper","blueflower","bamboo"}) add(field);
    auto locations=detail::locations(routes);
    return std::make_shared<workflows::RouteCatalog>(
        std::move(locations),std::move(routes));
}
std::shared_ptr<const workflows::RouteCatalog> walking_field_routes(double speed,std::shared_ptr<const movement::Recipes> recipes) {
    std::vector<workflows::ExecutableRoute> routes;
    for (unsigned slot=1;slot<=6;++slot) {
        const auto source=walking_field_routes(slot,speed,recipes);
        for (auto route : source->network().routes()) {
            const auto path=source->checkpoints(route.id);
            if (route.from=="hive.departure") {
                route.id="hive-"+std::to_string(slot)+"-"+route.id.substr(5); route.from="hive.proximity";
                route.requirements={"hive.slot."+std::to_string(slot)};
                route.departure_requirements={"hive.camera.prepared"};
                routes.push_back({std::move(route),path});
            } else {
                route.id+="-"+std::to_string(slot);
                route.departure_requirements=std::move(route.requirements); route.requirements={"hive.slot."+std::to_string(slot)};
                routes.push_back({std::move(route),path});
            }
        }
    }
    auto locations=detail::locations(routes);
    return std::make_shared<workflows::RouteCatalog>(
        std::move(locations),std::move(routes));
}
std::shared_ptr<const NavigationPerception> hive_navigation(std::shared_ptr<const NavigationPerception> perception,
    std::shared_ptr<HiveSetupResult> setup) {
    return std::make_shared<HiveNavigation>(std::move(perception),std::move(setup));
}
namespace {
class FieldReturn final : public Program {
    std::vector<Step> steps_;
    RunConfig config_;
public:
    FieldReturn(const std::string& field,double speed,unsigned hive_slot,bool planting,FieldReturnOptions options) {
        if (options.hive) if (const auto state=options.hive->read();state && state->claimed_slot) hive_slot=*state->claimed_slot;
        if (std::none_of(game::fields().begin(),game::fields().end(),[&](const auto& value) { return value.id==field; }) || hive_slot>6 || !std::isfinite(speed) || speed<8 || speed>200)
            throw std::invalid_argument("Field return requires a valid speed and optional hive slot");
        config_={Time(65000),speed,{},true,true};
        auto recipes=movement::snapshot(options.movement);
        auto paths=recipes->sequence("return-route",{{"field",field},{"slot",int(hive_slot)},{"bees",int(options.access.bees)},
            {"cannon",options.access.cannon},{"teleporter",bool(options.transport_prompt)},{"planting",planting}},speed);
        auto append=[&](workflows::CheckpointPlan path) {
            config_.duration+=path.timeout;
            if (path.completion==workflows::CheckpointCompletion::movement) steps_.push_back(FollowPath{std::move(path)});
            else if (path.completion==workflows::CheckpointCompletion::launch) {
                if (!options.transport_prompt) throw std::invalid_argument("Transport returns need interaction prompt detection");
                steps_.push_back(ApproachLandmark{std::move(path),options.transport_prompt});
            } else steps_.push_back(ApproachPrompt{std::move(path),game::hive_proximity_prompt()});
        };
        for (auto& step:paths) {
            if (const auto* lock=std::get_if<movement::ShiftLock>(&step))
                steps_.push_back(RunProgram{[enabled=lock->enabled] { return set_shift_lock(enabled); }});
            else if (auto* path=std::get_if<workflows::CheckpointPlan>(&step)) append(std::move(*path));
            else throw std::runtime_error("Return route contains an unsupported marker");
        }
        if (!hive_slot) append(recipes->path("hive",{{"action",std::string("row")}},speed).at(0));

    }
    RunConfig config() const override { return config_; }
    Step next(const View& view) override {
        if (view.completed_steps<steps_.size()) return steps_[view.completed_steps];
        return Done{"Owned hive confirmed"};
    }
};
}
std::unique_ptr<Program> return_from_field(const std::string& field,double speed,unsigned hive_slot,FieldReturnOptions options) {
    return std::make_unique<FieldReturn>(field,speed,hive_slot,false,std::move(options));
}
std::unique_ptr<Program> return_from_planting(const std::string& field,double speed,unsigned hive_slot,FieldReturnOptions options) {
    return std::make_unique<FieldReturn>(field,speed,hive_slot,true,std::move(options));
}

}
