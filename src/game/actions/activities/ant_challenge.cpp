#include "ant_challenge.hpp"
#include "game/actions/field/shift_lock.hpp"
#include "game/actions/interaction/ant_amulet.hpp"
#include "game/detection/dialog.hpp"
#include <algorithm>
#include "game/actions/travel/route_helpers.hpp"

namespace hive::automation {
namespace {
const game::PromptLandmark score{{"Score:"},{0,0,1,.5},.8f,game::PromptMatch::phrase};
class Challenge final : public Program {
    ProgramFactory prepare_,home_,restock_;
    bool restocked_{},missing_{},active_{},cleared_previous_{};
    double speed_;
    workflows::CheckpointPlan position_;
    unsigned phase_{};
    std::function<void(const ObservationContext&)> unavailable_;
    Time start_attempt_{};
    Time until_{};
public:
    Challenge(ProgramFactory prepare,ProgramFactory home,double speed,movement::Source source,std::function<void(const ObservationContext&)> unavailable,ProgramFactory restock):restock_(std::move(restock)),unavailable_(std::move(unavailable)),prepare_(std::move(prepare)),home_(std::move(home)),speed_(speed),position_(movement::snapshot(source)->path("ant-position",{},speed).at(0)) {
        if (!prepare_ || !home_ || !std::isfinite(speed) || speed<8 || speed>200) throw std::invalid_argument("Ant Challenge needs hive preparation and return");
    }
    std::vector<Rect> text_regions() const override {
        if (phase_==4) return {{.4,.6,.6,.4}};
        return {};
    }
    std::vector<game::FixedLabelQuery> fixed_labels() const override {
        if (phase_<4 || phase_>5) return {};
        auto labels=game::ant_amulet_labels();
        labels.push_back({{0,0,1,.5},{"Score"}});
        return labels;
    }
    RunConfig config() const override { return {std::chrono::minutes(12),speed_,{},true,true}; }
    bool expects_respawn() const noexcept override { return phase_>=4 && phase_<=6; }
    void observe(const View& view) override {
        if (phase_!=4 || !view.observations) return;
        const auto missing=game::read_prompt_landmark(view.context,view.observations->text,
            {{"Not enough Ant Passes","You need an Ant Pass","You don't have an Ant Pass","You don't have enough Ant Passes"},{0,0,1,1},.7f,game::PromptMatch::phrase});
        if (missing.has_value(view.context,Time(2000),start_attempt_)) {
            if (restock_ && !restocked_) { missing_=true; return; }
            if (unavailable_) unavailable_(view.context);
            throw ObservationUnavailable("No Ant Pass available; other work can continue");
        }
    }
    Step next(const View& view) override {
        const auto& c=view.context;
        if (!cleared_previous_) { cleared_previous_=true; return RunProgram{keep_ant_amulet}; }
        if (!phase_) { ++phase_; return RunProgram{[] { return set_shift_lock(false); }}; }
        if (phase_==1) { ++phase_; return RunProgram{prepare_}; }
        if (phase_==2) { ++phase_; return Navigate{"challenge.ant"}; }
        if (phase_==3) { ++phase_; start_attempt_=c.now; return Hold{{Key::interact},Time(80)}; }
        // A short run may end before a Score frame is observed. Handle its
        // result before start verification, movement or another pass attempt.
        if ((phase_==4 || phase_==5) && view.observations
            && game::read_ant_amulet(c,*view.observations).has_value(c,Time(2000),start_attempt_+Time(1))) {
            phase_=6;
            return RunProgram{keep_ant_amulet};
        }
        if (phase_==4) {
            observe(view);
            if (missing_) {
                missing_=false; restocked_=true; phase_=1;
                return RunProgram{[restock=restock_,unavailable=unavailable_,context=c] {
                    try { return restock(); }
                    catch (const ObservationUnavailable&) { if (unavailable) unavailable(context); throw; }
                }};
            }
            if (view.observations && game::read_prompt_landmark(c,view.observations->fixed_labels,score).has_value(c,Time(2000),start_attempt_+Time(1))) {
                active_=true; ++phase_; return FollowPath{position_};
            }
            if (c.now-start_attempt_>=Time(15000)) throw ObservationUnavailable("Ant Challenge did not start; another pass was not spent");
            return Wait{Time(100),"Waiting for Ant Challenge"};
        }
        if (phase_==6) phase_=7; // Shared cleanup completed and confirmed closure.
        if (phase_==7) { ++phase_; return RunProgram{home_}; }
        if (phase_==8) return Done{"Ant Challenge ended; quest progress must be checked"};
        if (!until_.count()) until_=c.now+std::chrono::minutes(5)+Time(15000);
        if (c.now>=until_) throw std::runtime_error("Ant Challenge outcome was not readable; another pass was not spent");
        if (!view.observations) return Wait{};
        active_=game::read_prompt_landmark(c,view.observations->fixed_labels,score).has_value(c,Time(2000))
            && std::none_of(view.observations->fixed_labels.value.lines.begin(),view.observations->fixed_labels.value.lines.end(),
                [](const auto& line) { return line.text=="Keep Old" || line.text=="Replace"; });
        if (!active_)
            return Wait{Time(100),"Waiting for Ant Challenge observations"};
        return Hold{{Key::primary},Time(250),Point{.95,.95}};
    }
};
}
std::unique_ptr<Program> ant_challenge(ProgramFactory prepare,ProgramFactory home,double speed,movement::Source source,std::function<void(const ObservationContext&)> unavailable,ProgramFactory restock) {
    return std::make_unique<Challenge>(std::move(prepare),std::move(home),speed,std::move(source),std::move(unavailable),std::move(restock));
}
std::shared_ptr<const workflows::RouteCatalog> ant_routes(double speed,std::shared_ptr<const movement::Recipes> recipes) {
    if (!recipes) recipes=movement::snapshot();
    std::vector<workflows::ExecutableRoute> routes;
    for (unsigned kind:{0u,1u,2u}) {
        const bool dispenser=kind==0,purchase=kind==2;
        const bool challenge=!dispenser && !purchase;
        auto approach=recipes->path("ant",{{"dispenser",dispenser},{"purchase",purchase}},speed);
        const std::string destination=dispenser?"station.ant-pass":purchase?"station.ant-pass-paid":"challenge.ant";
        // As with NPC routes, completed movement supplies the destination;
        // the shared E detector confirms interaction range without OCR.
        approach.push_back({challenge?"interaction.prompt":destination,{},{},Time(5000),Time(2000),0});
        Time timeout{1500}; for (const auto& step:approach) timeout+=step.timeout;
        const auto estimate=detail::estimate(approach)+Time(1500);
        routes.push_back({{dispenser?"hive-ant-pass":purchase?"hive-ant-pass-paid":"hive-ant","hive.proximity",destination,estimate,{},
            challenge?game::RouteArrival::estimated:game::RouteArrival::observed,{"hive.camera.prepared"},timeout},std::move(approach)});
    }
    auto locations=detail::locations(routes);
    return std::make_shared<workflows::RouteCatalog>(std::move(locations),std::move(routes));
}
}
