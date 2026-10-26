#include "plan.hpp"
#include "chat.hpp"
#include "game/detection/vicious.hpp"
#include "modules/shared/movement.hpp"
#include "game/data/fields.hpp"

namespace hive::features::combat {
namespace {
class Hunt final : public automation::Program {
    enum class Phase { travel,search,result,home,prepare,aligned };
    Enemy enemy_;
    double speed_;
    automation::ProgramFactory prepare_,home_;
    std::shared_ptr<const movement::Recipes> movement_;
    Phase phase_{Phase::travel};
    std::vector<std::string> remaining_;
    std::string field_,reported_;
    Time reported_at_{Time(-1)};
    Time travelled_{};
    std::optional<Time> began_;
    unsigned retries_{};
    bool retrying_{},chat_ready_{},chat_opening_{},defeat_seen_{};
    void choose(const automation::View& view) {
        auto preferred=enemy_.requested_field?enemy_.requested_field(view.context):std::string{};
        if (reported_at_>=Time{} && view.context.now-reported_at_<=Time(30000)) preferred=reported_;
        auto found=std::find(remaining_.begin(),remaining_.end(),preferred);
        if (found==remaining_.end()) found=remaining_.begin();
        field_=*found; remaining_.erase(found); retries_=0;
    }
public:
    Hunt(Enemy enemy,double speed,automation::ProgramFactory prepare,automation::ProgramFactory home,movement::Source source)
        :enemy_(std::move(enemy)),speed_(speed),prepare_(std::move(prepare)),home_(std::move(home)),movement_(movement::snapshot(source)),remaining_(enemy_.hunt_fields) {
        if (!prepare_ || !home_ || remaining_.empty() || !enemy_.observations || !enemy_.progress)
            throw std::invalid_argument("Hunting needs fields, observations and hive travel");
        std::set<std::string> fields(remaining_.begin(),remaining_.end());
        if (fields.size()!=remaining_.size()) throw std::invalid_argument("Duplicate hunt field");
        enemy_.progress->begin(0);
    }
    automation::RunConfig config() const override { return {std::chrono::minutes(15),speed_,{},true,true}; }
    ActivityStatus activity_status() const override {
        if (phase_==Phase::result || phase_==Phase::home || phase_==Phase::prepare || phase_==Phase::aligned) return {};
        const auto field=game::field_at(field_+".entry");
        return {phase_==Phase::search?"Travelling":"Searching",enemy_.species+(field?" · "+field->name:"")};
    }
    void observe(const automation::View& view) override {
        if (enemy_.vicious) {
            // The notification belongs to the whole hunt, including travel and
            // returns between fields. Keep this small pixel channel requested
            // so a reset/alignment does not leave a new defeat unobserved.
            if (chat_ready_) {
                const auto seen=game::read_vicious(*enemy_.vicious,view.context);
                if (seen.has_value(view.context,Time(2000)) && began_ && seen.value.defeated
                    && *seen.value.defeated>=*began_ && *seen.value.defeated<=view.context.now)
                    defeat_seen_=true;
            }
            return;
        }
        if (phase_==Phase::home || phase_==Phase::prepare || phase_==Phase::aligned) return;
        if (const auto seen=game::read_enemies(*enemy_.observations,view.context))
            for (const auto& activity:seen->activity) if (activity.name==enemy_.species) {
                reported_=activity.field; reported_at_=seen->visible.captured;
            }
    }
    void step_completed(const automation::View&) override {
        if (chat_opening_) { chat_opening_=false; chat_ready_=true; }
    }
    automation::ProgramFactory recovery(automation::Interruption reason,const automation::View& view) override {
        if (reason==automation::Interruption::disconnected || reason==automation::Interruption::client_rejoin
            || field_.empty() || retries_>=2 || (began_ && view.context.now-*began_>=std::chrono::minutes(5))) return {};
        const auto progress=enemy_.progress->read();
        if (progress.defeated || progress.unavailable || defeat_seen_) { phase_=Phase::result; return home_; }
        ++retries_; retrying_=true; phase_=Phase::prepare;
        return home_;
    }
    automation::Step next(const automation::View& view) override {
        using namespace automation;
        if (!began_) { began_=view.context.now; choose(view); }
        const auto progress=enemy_.progress->read();
        if (progress.defeated) return Done{"Defeated "+enemy_.species};
        if (progress.unavailable || defeat_seen_) {
            // Only the encounter may credit a kill. A notification elsewhere
            // still ends the search, without counting another player's kill.
            enemy_.progress->finish_unavailable(enemy_.cooldown);
            return Done{enemy_.species+" was defeated; ending the search"};
        }
        if (enemy_.vicious && !chat_ready_) {
            chat_opening_=true;
            return RunProgram{show_chat};
        }
        if (enemy_.vicious && phase_==Phase::travel
            && !game::read_vicious(*enemy_.vicious,view.context).has_value(view.context,Time(2000))) {
            if (view.context.now-*began_>=Time(5000)) throw ObservationUnavailable("Combat notifications could not be captured");
            return Wait{Time(100),"Preparing combat notifications"};
        }
        const bool expired=view.context.now-*began_>=std::chrono::minutes(5);
        if (phase_==Phase::result) {
            if (remaining_.empty() || expired) {
                enemy_.progress->finish(false,std::chrono::minutes(10));
                return Done{expired?"Vicious Bee search time limit reached":"No Vicious Bee in the selected fields"};
            }
            phase_=Phase::home; return RunProgram{home_};
        }
        if (phase_==Phase::home) phase_=Phase::prepare;
        if (phase_==Phase::prepare) {
            if (!retrying_) choose(view);
            retrying_=false; phase_=Phase::aligned; return RunProgram{prepare_};
        }
        if (phase_==Phase::aligned) phase_=Phase::travel;
        if (phase_==Phase::travel) {
            if (expired) { enemy_.progress->finish(false,std::chrono::minutes(10)); return Done{"Vicious Bee search time limit reached"}; }
            travelled_=view.context.now; phase_=Phase::search; return Navigate{field_+".entry"};
        }
        auto encounter_enemy=enemy_;
        encounter_enemy.scheduled={}; encounter_enemy.field=field_;
        if (!enemy_.vicious) encounter_enemy.requested_field=[source=enemy_.observations,remaining=remaining_,requested=enemy_.requested_field](const ObservationContext& context) {
            auto field=requested?requested(context):std::string{};
            if (const auto seen=game::read_enemies(*source,context))
                for (const auto& activity:seen->activity) if (activity.name=="Vicious Bee") field=activity.field;
            return std::find(remaining.begin(),remaining.end(),field)!=remaining.end()?field:std::string{};
        };
        encounter_enemy.search=movement_walks(*movement_,"vicious-search",{{"field",field_}},true);
        phase_=Phase::result;
        return RunProgram{[enemy=std::move(encounter_enemy),speed=speed_,entered=travelled_] { return encounter(enemy,speed,entered); }};
    }
};
}
std::unique_ptr<automation::Program> hunt(const Enemy& enemy,double speed,automation::ProgramFactory prepare,automation::ProgramFactory home,movement::Source movement) {
    return std::make_unique<Hunt>(enemy,speed,std::move(prepare),std::move(home),std::move(movement));
}
}
