#include "attack_assist.hpp"
#include "game/data/fields.hpp"
#include "game/actions/inventory/item_reserves.hpp"
#include "game/detection/passive_counters.hpp"
#include <algorithm>
#include <utility>

namespace hive::features::combat {
using coordination::AttackAssistState;
namespace {
struct Saw {
    unsigned slot{},used{};
    Time next{};
    std::shared_ptr<game::ItemStock> stock;
    std::shared_ptr<const game::DetectionSource> observations,passives;
    std::shared_ptr<AttackAssistState> state;
    std::string species;
    std::optional<unsigned> pending;
    Time pressed{};
    unsigned hotbar(const automation::View& view) {
        if(!slot || !passives || !state->allowed || !state->main_ready) return 0;
        const auto reading=passives->read();
        if(pending && view.context.now-pressed>Time(5000)) { slot=0;return 0; }
        if(!reading || !reading->error.empty())return 0;
        const Observation<bool> stamp{Knowledge::present,true,reading->context.now,reading->context.window,reading->context.generation};
        if(!stamp.has_value(view.context,Time(1500),pressed))return 0;
        const auto* counters=dynamic_cast<const game::PassiveCounters*>(reading->value.get());
        if(!counters)return 0;
        const auto found=std::find_if(counters->counters.begin(),counters->counters.end(),[](const auto& c){return c.name=="Star Saw";});
        if(found==counters->counters.end() || !found->charges || *found->charges>2)return 0;
        const auto charges=*found->charges;
        if(pending) {
            if(reading->context.now<=pressed || charges!=*pending)return 0;
            // The game's counter wraps on activation. Do not infer success
            // from a key press or spend another set during this visit.
            if(charges==0) { slot=0;return 0; }
            pending.reset();
        }
        if(used==3 || view.context.now<next)return 0;
        const auto enemies=game::read_enemies(*observations,view.context);
        if(!enemies || std::none_of(enemies->visible.value.begin(),enemies->visible.value.end(),
            [&](const auto& enemy) { return enemy.name==species; })) return 0;
        if(stock->blocked({{"Stinger",3-charges}}) || !stock->spend({{"Stinger",1}},view.context.now)) { slot=0;return 0; }
        ++used;pressed=view.context.now;pending=(charges+1)%3;next=pressed+Time(1000);return slot;
    }
};
class Preparation final : public automation::Program {
    double speed_;
    std::vector<workflows::CheckpointPlan> positioning_;
    std::function<Time()> clock_;
    coordination::AttackJob job_;
    std::string destination_;
    bool attack_;
    std::shared_ptr<AttackAssistState> state_;
    std::shared_ptr<Saw> saw_;
    std::shared_ptr<const game::InventorySource> inventory_;
    enum class Stage { supplies,travel,prepare } stage_{Stage::supplies};
    std::size_t position_{};
public:
    Preparation(double speed,std::vector<workflows::CheckpointPlan> positioning,std::function<Time()> clock,coordination::AttackJob job,std::string destination,bool attack,
        std::shared_ptr<AttackAssistState> state,std::shared_ptr<Saw> saw,std::shared_ptr<const game::InventorySource> inventory)
        :speed_(speed),positioning_(std::move(positioning)),clock_(std::move(clock)),job_(std::move(job)),destination_(std::move(destination)),attack_(attack),
        state_(std::move(state)),saw_(std::move(saw)),inventory_(std::move(inventory)) {}
    automation::RunConfig config() const override { return {std::chrono::minutes(18),speed_,{},true,true}; }
    std::string field() const override {
        if(state_->arrived)for(const auto& field:game::fields())
            if(field.location()==destination_ || field.planting_location()==destination_)return field.name;
        return {};
    }
    WorkActivity activity() const noexcept override { return stage_==Stage::prepare && attack_?WorkActivity::gathering:WorkActivity::other; }
    ActivityStatus activity_status() const override { return {"Preparing attack",job_.enemy}; }
    automation::Step next(const automation::View&) override {
        using namespace automation;
        if(stage_==Stage::supplies) {
            stage_=Stage::travel;
            if(saw_->slot && !saw_->stock->unread({{"Stinger",3}}).empty())
                return RunProgram{[stock=saw_->stock,inventory=inventory_] { return inspect_reserves(stock,inventory,{{"Stinger",3}}); },true};
        }
        if(stage_==Stage::travel) { stage_=Stage::prepare;return Navigate{destination_}; }
        if(position_<positioning_.size()) return FollowPath{positioning_[position_++]};
        state_->arrived=true;
        if(clock_()<job_.starts) {
            if(!attack_) return Wait{Time(200),"Waiting for "+job_.enemy};
            return Hold{{Key::primary},Time(250),Point{.85,.15}};
        }
        return Done{"Ready for "+job_.enemy};
    }
    void step_completed(const automation::View& view) override {
        // An unavailable item count disables the optional spend, not the fight.
        if(view.completed && view.completed->state!=automation::State::completed) saw_->slot=0;
    }
};
class AssistGuard final : public automation::RunGuard {
    std::shared_ptr<AttackAssistState> state_;
    std::function<Time()> clock_;
    Time end_;
public:
    AssistGuard(std::shared_ptr<AttackAssistState> state,std::function<Time()> clock,Time end)
        :state_(std::move(state)),clock_(std::move(clock)),end_(end) {}
    automation::GuardDecision evaluate(const automation::View&,Time) override {
        if(!state_->allowed || clock_()>=end_) return {automation::GuardState::completed,"Attack support ended"};
        return {};
    }
};
class Assist final : public automation::Program {
    automation::ProgramFactory visit_;
    std::shared_ptr<AttackAssistState> state_;
    std::shared_ptr<FightProgress> progress_;
    std::function<Time()> clock_;
    Time end_;
    double speed_;
    std::string enemy_,destination_;
    bool follow_main_;
public:
    Assist(automation::ProgramFactory visit,std::shared_ptr<AttackAssistState> state,std::shared_ptr<FightProgress> progress,
        std::function<Time()> clock,const coordination::AttackJob& job,double speed,std::string destination,bool follow_main)
        :visit_(std::move(visit)),state_(std::move(state)),progress_(std::move(progress)),clock_(std::move(clock)),end_(job.ends),speed_(speed),
        enemy_(job.enemy),destination_(std::move(destination)),follow_main_(follow_main) {}
    automation::RunConfig config() const override { return {std::chrono::hours(8),speed_,{},true,false}; }
    std::unique_ptr<automation::RunGuard> guard() const override { return std::make_unique<AssistGuard>(state_,clock_,end_); }
    std::string field() const override { const auto field=game::field_at(destination_);return state_->arrived && field?field->name:std::string{}; }
    ActivityStatus activity_status() const override { return {"Attack support",enemy_}; }
    automation::Step next(const automation::View&) override {
        if(visit_) return automation::RunProgram{std::exchange(visit_,{}),true};
        // A helper may see its own Commando die first. The main owns completion
        // of a requested fight; keep supporting from the stationary fight spot.
        if(follow_main_ && state_->allowed) return automation::Wait{Time(200),"Helping main · "+enemy_};
        return automation::Done{state_->defeated?enemy_+" defeated":"Attack visit ended"};
    }
    void observe(const automation::View&) override { if(progress_->read().defeated) state_->defeated=true; }
    void step_completed(const automation::View& view) override {
        observe(view);
        if(view.completed && view.completed->state!=automation::State::completed) follow_main_=false;
    }
    void finished(automation::State) override { observe({});state_->finished=true; }
};
}
coordination::AttackAssist attack_assist(Plan plan,std::shared_ptr<const game::RouteNetwork> network,
    std::shared_ptr<const automation::AvailabilitySource> availability,std::shared_ptr<visits::Cooldowns> saved,
    std::shared_ptr<game::ItemStock> stock,std::shared_ptr<const game::InventorySource> inventory) {
    if(!network || !availability) return {};
    std::erase_if(plan.enemies,[](const auto& enemy) {
        return !enemy.shared || !enemy.hunt_fields.empty() || !enemy.observations || enemy.fight.legs.empty();
    });
    if(plan.enemies.empty()) return {};
    coordination::AttackAssist result;
    for(const auto& enemy:plan.enemies) result.destinations.emplace(enemy.species,enemy.destination);
    plan.options.enemies.clear();plan.options.boss_order.clear();
    result.create=[plan=std::move(plan),network=std::move(network),availability=std::move(availability),saved=std::move(saved),stock=std::move(stock),inventory=std::move(inventory)]
        (const coordination::AttackJob& job,bool attack,unsigned slot,std::shared_ptr<AttackAssistState> state) -> std::unique_ptr<automation::Program> {
        if(!state || slot>7 || (attack && slot && (!stock || !inventory)) || job.ends<=job.starts) throw std::invalid_argument("Invalid attack support setup");
        auto configured=plan;
        std::erase_if(configured.enemies,[&](const auto& enemy) { return enemy.species!=job.enemy; });
        if(configured.enemies.empty()) return {};
        auto& enemy=configured.enemies.front();enemy.progress=std::make_shared<FightProgress>();
        auto destination=enemy.destination;
        std::vector<workflows::CheckpointPlan> positioning;
        if(attack && enemy.species=="Mondo Chick") {
            destination="mountaintop.planting";
            if(!network->contains(destination)) throw std::invalid_argument("Mondo support needs the Mountain Top planter route");
            positioning=movement::snapshot(plan.movement)->path("bosses",{{"enemy",enemy.species}},plan.config.speed);
            enemy.stationary=true;
        }
        enemy.scheduled=[clock=plan.wall_clock,job](const ObservationContext&) { return clock()<job.ends; };
        auto saw=std::make_shared<Saw>(Saw{attack?slot:0,0,{},stock,enemy.observations,plan.passives,state,enemy.species});
        enemy.fight.hotbar=[saw](const automation::View& view) { return saw->hotbar(view); };
        if(attack) { configured.finish_at.reset();enemy.departure={};configured.options.reset_return=false; }
        configured.options.enemies[enemy.id].collect_loot=!attack;
        if(job.scheduled) configured.options.enemies[enemy.id].boss=MondoOptions{};
        if(attack && !job.scheduled) enemy.maximum_health=enemy.initial_health=0;
        auto visits=visit_plan(configured);
        visits.visits.front().approach=[speed=plan.config.speed,positioning=std::move(positioning),clock=plan.wall_clock,job,destination,attack,state,saw,inventory] {
            return std::make_unique<Preparation>(speed,positioning,clock,job,destination,attack,state,saw,inventory);
        };
        auto visit=[visits=std::move(visits),network,availability,saved=attack && !job.scheduled?nullptr:saved]() {
            auto run=automation::visit_program(visits,network,availability);
            return saved?visits::save_cooldowns(std::move(run),saved):std::move(run.program);
        };
        return std::make_unique<Assist>(std::move(visit),state,enemy.progress,plan.wall_clock,job,plan.config.speed,enemy.destination,attack && !job.scheduled && enemy.stationary);
    };
    return result;
}
}
