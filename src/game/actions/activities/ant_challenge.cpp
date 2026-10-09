#include "ant_challenge.hpp"
#include "game/actions/field/shift_lock.hpp"
#include "game/detection/dialog.hpp"
#include "game/actions/travel/route_helpers.hpp"

namespace hive::automation {
namespace {
const game::PromptLandmark start{{"Spend 1 Ant Pass to enter the Ant Field."}};
const game::PromptLandmark score{{"Score:"},{0,0,1,.5},.8f,game::PromptMatch::phrase};
const game::PromptLandmark amulet{{"Ant Amulet"},{.15,.1,.7,.85},.8f,game::PromptMatch::phrase};
class Challenge final : public Program {
    ProgramFactory prepare_,home_;
    double speed_;
    workflows::CheckpointPlan position_;
    unsigned phase_{};
    std::function<void(const ObservationContext&)> unavailable_;
    Time start_attempt_{};
    Time frame_{Time(-1)},until_{};
public:
    Challenge(ProgramFactory prepare,ProgramFactory home,double speed,movement::Source source,std::function<void(const ObservationContext&)> unavailable):unavailable_(std::move(unavailable)),prepare_(std::move(prepare)),home_(std::move(home)),speed_(speed),position_(movement::snapshot(source)->path("ant-position",{},speed).at(0)) {
        if (!prepare_ || !home_ || !std::isfinite(speed) || speed<8 || speed>200) throw std::invalid_argument("Ant Challenge needs hive preparation and return");
    }
    std::vector<Rect> text_regions() const override { return {{.2,0,.6,.3},{.4,.6,.6,.4}}; }
    RunConfig config() const override { return {std::chrono::minutes(12),speed_,{},true,true}; }
    void observe(const View& view) override {
        if (phase_!=4 || !view.observations) return;
        const auto missing=game::read_prompt_landmark(view.context,view.observations->text,
            {{"Not enough Ant Passes","You need an Ant Pass","You don't have an Ant Pass","You don't have enough Ant Passes"},{0,0,1,1},.7f,game::PromptMatch::phrase});
        if (missing.has_value(view.context,Time(2000),start_attempt_)) {
            if (unavailable_) unavailable_(view.context);
            throw ObservationUnavailable("No Ant Pass available; other work can continue");
        }
    }
    Step next(const View& view) override {
        const auto& c=view.context;
        if (!phase_) { ++phase_; return RunProgram{[] { return set_shift_lock(false); }}; }
        if (phase_==1) { ++phase_; return RunProgram{prepare_}; }
        if (phase_==2) { ++phase_; return Navigate{"challenge.ant"}; }
        if (phase_==3) { ++phase_; start_attempt_=c.now; return UsePrompt{start,score,Time(15000)}; }
        if (phase_==4) { ++phase_; return FollowPath{position_}; }
        if (phase_==7) { ++phase_; return RunProgram{home_}; }
        if (phase_==8) return Done{"Ant Challenge ended; quest progress must be checked"};
        if (!until_.count()) until_=c.now+std::chrono::minutes(5)+Time(15000);
        if (c.now>=until_) throw std::runtime_error("Ant Challenge outcome was not readable; another pass was not spent");
        if (!view.observations) return Wait{};
        const auto buttons=game::read_dialog(c,view.observations->text,amulet,{"Keep Old","Replace"});
        if (phase_==6) {
            if (buttons.knowledge==Knowledge::absent && buttons.usable(c,Time(2000)) && buttons.captured>frame_) {
                frame_=buttons.captured; phase_=7;
            }
            return Wait{Time(100),"Confirming Ant Amulet closed"};
        }
        if (buttons.has_value(c,Time(2000))) {
            if (buttons.captured<=frame_) return Wait{};
            frame_=buttons.captured;
            const auto point=buttons.value.yes;
            phase_=6; until_=c.now+Time(10000);
            return UseControl{{Knowledge::present,point,buttons.captured,c.window,c.generation},0,"Keep existing Ant Amulet",Point{.85,.15}};
        }
        if (!game::read_prompt_landmark(c,view.observations->text,score).has_value(c,Time(2000)) || buttons.knowledge!=Knowledge::absent)
            return Wait{Time(100),"Waiting for Ant Challenge observations"};
        return Hold{{Key::primary},Time(250),Point{.95,.95}};
    }
};
}
std::unique_ptr<Program> ant_challenge(ProgramFactory prepare,ProgramFactory home,double speed,movement::Source source,std::function<void(const ObservationContext&)> unavailable) {
    return std::make_unique<Challenge>(std::move(prepare),std::move(home),speed,std::move(source),std::move(unavailable));
}
std::shared_ptr<const workflows::RouteCatalog> ant_routes(double speed,std::shared_ptr<const movement::Recipes> recipes) {
    if (!recipes) recipes=movement::snapshot();
    std::vector<workflows::ExecutableRoute> routes;
    for (bool dispenser:{true,false}) {
        auto approach=recipes->path("ant",{{"dispenser",dispenser}},speed);
        const std::string destination=dispenser?"station.ant-pass":"challenge.ant";
        approach.push_back({destination,{},{},Time(5000),Time(2000),0});
        Time timeout{1500}; for (const auto& step:approach) timeout+=step.timeout;
        const auto estimate=detail::estimate(approach)+Time(1500);
        routes.push_back({{dispenser?"hive-ant-pass":"hive-ant","hive.proximity",destination,estimate,{},
            game::RouteArrival::observed,{"hive.camera.prepared"},timeout},std::move(approach)});
    }
    auto locations=detail::locations(routes);
    return std::make_shared<workflows::RouteCatalog>(std::move(locations),std::move(routes));
}
}
