#include "plan.hpp"
#include "chat.hpp"
#include "search_daylight.hpp"
#include "game/actions/field/shift_lock.hpp"
#include "game/actions/hive/hive_setup.hpp"
#include "game/detection/vicious.hpp"
#include "modules/shared/movement.hpp"
#include "game/data/fields.hpp"
#include <array>

namespace hive::features::combat {
namespace {
constexpr std::array<std::string_view,5> circuit{"pepper","mountaintop","spider","cactus","rose"};
constexpr Time search_budget=std::chrono::minutes(5);
class Hunt final : public automation::Program {
    enum class Phase { choose,route,scan,travel,search,result,home,prepare,aligned };
    Enemy enemy_;
    SearchDaylight daylight_;
    double speed_;
    automation::ProgramFactory prepare_,home_;
    HuntStart start_;
    std::shared_ptr<const movement::Recipes> movement_;
    Phase phase_{Phase::choose};
    std::vector<movement::Step> route_;
    std::size_t route_step_{};
    unsigned segment_{},empty_views_{},candidate_views_{};
    bool circuit_used_{},field_entry_{},view_changed_{};
    Time scan_after_{},scan_frame_{Time(-1)};
    std::optional<Point> candidate_;
    void load_route(std::string from,unsigned slot=0) {
        route_=movement_->sequence("vicious-scout",{{"from",std::move(from)},{"slot",int(slot)}},speed_);
        route_step_=0;phase_=Phase::route;
    }
    void next_segment() {
        if (segment_==circuit.size()) { field_entry_=false;phase_=remaining_.empty()?Phase::result:Phase::home;return; }
        const auto from=segment_?std::string(circuit[segment_-1]):"spawn";
        field_=circuit[segment_++];load_route(from);
    }
    std::vector<std::string> remaining_;
    std::string field_,reported_;
    Time reported_at_{Time(-1)};
    Time travelled_{};
    std::optional<Time> began_;
    Time active_began_{};
    unsigned retries_{};
    bool retrying_{},chat_ready_{},chat_opening_{},defeat_seen_{};
    void choose(const automation::View& view) {
        auto preferred=enemy_.requested_field?enemy_.requested_field(view.context):std::string{};
        if (reported_at_>=Time{} && view.context.now-reported_at_<=Time(30000)) preferred=reported_;
        auto found=std::find(remaining_.begin(),remaining_.end(),preferred);
        if (found==remaining_.end()) found=remaining_.begin();
        field_=*found; retries_=0;
    }
public:
    Hunt(Enemy enemy,double speed,automation::ProgramFactory prepare,automation::ProgramFactory home,movement::Source source,HuntStart start)
        :enemy_(std::move(enemy)),speed_(speed),prepare_(std::move(prepare)),home_(std::move(home)),start_(start),movement_(movement::snapshot(source)),remaining_(enemy_.hunt_fields) {
        if (!prepare_ || !home_ || remaining_.empty() || !enemy_.observations || !enemy_.progress)
            throw std::invalid_argument("Hunting needs fields, observations and hive travel");
        std::set<std::string> fields(remaining_.begin(),remaining_.end());
        if (fields.size()!=remaining_.size()) throw std::invalid_argument("Duplicate hunt field");
        if (start_==HuntStart::spawn && !uses_scout_route(enemy_))
            throw std::invalid_argument("Spawn scouting needs an accessible scout route");
        enemy_.progress->begin(0);
    }
    automation::RunConfig config() const override { return {std::chrono::minutes(15),speed_,{},true,true}; }
    ActivityStatus activity_status() const override {
        if (phase_==Phase::result || phase_==Phase::home || phase_==Phase::prepare || phase_==Phase::aligned) return {};
        const auto field=game::field_at(field_+".entry");
        return {phase_==Phase::route?"Travelling":"Searching",enemy_.species+(field?" · "+field->name:"")};
    }
    void observe(const automation::View& view) override {
        if (began_) daylight_.observe(enemy_.daylight,view.context,*began_);
        if (enemy_.vicious) {
            // The notification belongs to the whole hunt, including travel and
            // returns between fields. Keep this pixel channel requested
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
    void step_completed(const automation::View& view) override {
        if (chat_opening_) { chat_opening_=false; chat_ready_=true; }
        if (view_changed_) { scan_after_=view.context.now+Time(150);view_changed_=false; }
    }
    automation::ProgramFactory recovery(automation::Interruption reason,const automation::View& view) override {
        if (reason==automation::Interruption::disconnected || reason==automation::Interruption::client_rejoin
            || field_.empty() || retries_>=2 || (began_ && view.elapsed-active_began_>=search_budget)) return {};
        const auto progress=enemy_.progress->read();
        if (progress.defeated || progress.unavailable || defeat_seen_) { phase_=Phase::result; return home_; }
        ++retries_; retrying_=true; circuit_used_=true; field_entry_=true; phase_=Phase::prepare;
        return home_;
    }
    automation::Step next(const automation::View& view) override {
        using namespace automation;
        // Capture time identifies this hunt's notifications; the runner's
        // active clock budgets work without charging paused menu/capture waits.
        if (!began_) { began_=view.context.now; active_began_=view.elapsed; choose(view); }
        const auto progress=enemy_.progress->read();
        if (progress.defeated) return Done{"Defeated "+enemy_.species};
        if (progress.unavailable || defeat_seen_) {
            // Only the encounter may credit a kill. A notification elsewhere
            // still ends the search, without counting another player's kill.
            enemy_.progress->finish_unavailable(enemy_.cooldown);
            return Done{enemy_.species+" was defeated; ending the search"};
        }
        if (daylight_.ended()) {
            enemy_.progress->finish(false);
            return Done{"Daytime — ending Vicious Bee search"};
        }
        if (enemy_.vicious && !chat_ready_) {
            chat_opening_=true;
            return RunProgram{show_chat};
        }
        if (enemy_.vicious && phase_==Phase::choose
            && !game::read_vicious(*enemy_.vicious,view.context).has_value(view.context,Time(2000))) {
            if (view.elapsed-active_began_>=Time(5000)) throw ObservationUnavailable("Combat notifications could not be captured");
            return Wait{Time(100),"Preparing combat notifications"};
        }
        const bool expired=view.elapsed-active_began_>=search_budget;
        if (phase_==Phase::result) {
            std::erase(remaining_,field_);
            if (remaining_.empty() || expired) {
                enemy_.progress->finish(false,std::chrono::minutes(10));
                return Done{expired?"Vicious Bee search time limit reached":"No Vicious Bee in the selected fields"};
            }
            phase_=Phase::home;
        }
        if (phase_==Phase::home) { phase_=Phase::prepare;return RunProgram{home_}; }
        if (phase_==Phase::prepare) {
            if (!retrying_ && !field_entry_) choose(view);
            retrying_=false;field_entry_=true;phase_=Phase::aligned;return RunProgram{prepare_};
        }
        if (phase_==Phase::aligned) phase_=Phase::choose;
        if (phase_==Phase::choose) {
            if (expired) { enemy_.progress->finish(false,std::chrono::minutes(10));return Done{"Vicious Bee search time limit reached"}; }
            if (!circuit_used_ && uses_scout_route(enemy_)) {
                if (start_==HuntStart::spawn) load_route("spawn-start");
                else {
                    const auto hive=enemy_.scout_hive->read(view.context,view.observations);
                    if (!hive || !hive->claimed_slot || !hive->camera_prepared)
                        throw ObservationUnavailable("Vicious Bee scout route needs the prepared hive position");
                    load_route("hive",*hive->claimed_slot);
                }
                circuit_used_=true;segment_=0;
            } else phase_=Phase::travel;
        }
        if (phase_==Phase::route) {
            if (route_step_<route_.size()) {
                auto& step=route_[route_step_];
                if (const auto* lock=std::get_if<movement::ShiftLock>(&step)) {
                    ++route_step_;view_changed_=true;return RunProgram{[enabled=lock->enabled] { return set_shift_lock(enabled); }};
                }
                if (const auto* marker=std::get_if<movement::Marker>(&step)) {
                    if (marker->name!="cannon" || !enemy_.scout_transport) throw ObservationUnavailable("Vicious Bee scout cannon is unavailable");
                    const auto prompt=enemy_.scout_transport->read(view.context);
                    if (!prompt.has_value(view.context,Time(1000),scan_after_) || !prompt.value) {
                        if (view.context.now-scan_after_>Time(3000)) throw ObservationUnavailable("Vicious Bee scout cannon was not reached");
                        return Wait{Time(50),"Checking the scout cannon"};
                    }
                    ++route_step_;view_changed_=true;return Hold{{Key::interact},Time(100)};
                }
                ++route_step_;view_changed_=true;return FollowPath{std::get<workflows::CheckpointPlan>(step),[this] { return daylight_.ended() || defeat_seen_; }};
            }
            if (!segment_ || std::find(remaining_.begin(),remaining_.end(),field_)==remaining_.end()) {
                next_segment();return Wait{Time(1),"Moving to the next field view"};
            }
            phase_=Phase::scan;scan_frame_=Time(-1);empty_views_=candidate_views_=0;candidate_.reset();
        }
        if (phase_==Phase::scan) {
            const auto reading=game::read_vicious(*enemy_.vicious,view.context);
            if (!reading.has_value(view.context,Time(1000),scan_after_) || !reading.value.world_visible || reading.captured<=scan_frame_) {
                if (view.context.now-scan_after_>Time(5000)) throw ObservationUnavailable("Vicious Bee field view was unavailable");
                return Wait{Time(50),"Checking the field view"};
            }
            scan_frame_=reading.captured;
            const game::ViciousCandidate* found=nullptr;
            for (const auto& hit:reading.value.candidates) if (hit.center.x>=.5 && hit.center.y<=.57) {
                if (!found || (candidate_ && std::hypot(hit.center.x-candidate_->x,hit.center.y-candidate_->y)
                    <std::hypot(found->center.x-candidate_->x,found->center.y-candidate_->y))) found=&hit;
            }
            if (reading.value.visible) candidate_views_=2;
            else if (found) {
                candidate_views_=candidate_ && std::hypot(found->center.x-candidate_->x,found->center.y-candidate_->y)<.025?candidate_views_+1:1;
                candidate_=found->center;empty_views_=0;
            } else { ++empty_views_;candidate_views_=0;candidate_.reset(); }
            if (candidate_views_>=2) { field_entry_=true;phase_=Phase::home;return ReportActivity{{"Vicious Bee candidate",field_}}; }
            if (empty_views_>=2) {
                std::erase(remaining_,field_);
                if (remaining_.empty()) {phase_=Phase::result;return next(view);}
                next_segment();return Wait{Time(1),"Field view checked"};
            }
            if (view.context.now-scan_after_>Time(2000)) {
                // Ambiguous colour evidence gets an in-field check, not a false clear.
                field_entry_=true;phase_=Phase::home;return Wait{Time(1),"Checking a possible Vicious Bee"};
            }
            return Wait{Time(50),"Confirming the field view"};
        }
        if (phase_==Phase::travel) {
            field_entry_=false;travelled_=view.context.now;phase_=Phase::search;
            return Navigate{field_+".entry",{},[this] { return daylight_.ended() || defeat_seen_; }};
        }
        auto encounter_enemy=enemy_;
        encounter_enemy.scheduled={}; encounter_enemy.field=field_;
        if (!enemy_.vicious) encounter_enemy.requested_field=[source=enemy_.observations,remaining=remaining_,requested=enemy_.requested_field](const ObservationContext& context) {
            auto field=requested?requested(context):std::string{};
            if (const auto seen=game::read_enemies(*source,context))
                for (const auto& activity:seen->activity) if (activity.name=="Vicious Bee") field=activity.field;
            return std::find(remaining.begin(),remaining.end(),field)!=remaining.end()?field:std::string{};
        };
        // Colour sightings identify a field to visit. Use the established field
        // route to find/activate the bee; palette patches are not reliable targets.
        encounter_enemy.search=movement_walks(*movement_,"vicious-search",{{"field",field_}},true);
        phase_=Phase::result;
        return RunProgram{[enemy=std::move(encounter_enemy),speed=speed_,entered=travelled_] { return encounter(enemy,speed,entered); }};
    }
};
}
bool uses_scout_route(const Enemy& enemy) {
    return enemy.vicious && enemy.scout_route && enemy.scout_hive
        && std::any_of(circuit.begin(),circuit.end(),[&](auto field) {
            return std::find(enemy.hunt_fields.begin(),enemy.hunt_fields.end(),field)!=enemy.hunt_fields.end();
        });
}
std::unique_ptr<automation::Program> hunt(const Enemy& enemy,double speed,automation::ProgramFactory prepare,automation::ProgramFactory home,movement::Source movement,HuntStart start) {
    return std::make_unique<Hunt>(enemy,speed,std::move(prepare),std::move(home),std::move(movement),start);
}
}
