#include "plan.hpp"
#include "chat.hpp"
#include "game/detection/vicious.hpp"
#include <algorithm>
#include <array>
#include "game/detection/dialog.hpp"
#include "game/data/fields.hpp"

namespace hive::features::combat {
namespace {
class Encounter final : public automation::Program {
    Enemy enemy_;
    double speed_;
    Time entered_,started_{},last_frame_{Time(-1)},moved_{};
    std::optional<Time> clear_since_,completed_at_,engaged_;
    unsigned visible_peak_{},visible_{},leg_{},search_{};
    std::set<std::uint64_t> defeats_;
    double x_{},y_{};
    bool started{},complete_{},amulet_kept_{},amulet_seen_{},session_finished_{},unavailable_{},search_origin_{};
    std::optional<Time> search_finished_;
    std::optional<game::WalkLeg> pending_;
    std::shared_ptr<game::DistanceIntegrator> distance_;
    Time observed_now_{},chat_at_{};
    bool search_leg_{};
    automation::Walk walk(automation::Walk step,bool searching=false) {
        pending_=step.leg; search_leg_=searching;
        if (!enemy_.search.empty()) {
            distance_=std::make_shared<game::DistanceIntegrator>(step.leg.tiles);
            step.distance=distance_;
            step.until=[this,searching] {
                return complete_ || session_finished_ || (searching && engaged_)
                    || observed_now_-last_frame_>Time(2000);
            };
        }
        return step;
    }
public:
    Encounter(Enemy enemy,double speed,Time entered):enemy_(std::move(enemy)),speed_(speed),entered_(entered) {
        if (!enemy_.observations || enemy_.count<1 || enemy_.count>8 || enemy_.fight.legs.empty())
            throw std::invalid_argument("Encounter needs observations and a movement plan");
        if (enemy_.progress) enemy_.progress->begin(enemy_.maximum_health,enemy_.initial_health);
    }
    automation::RunConfig config() const override { return {enemy_.keep_old?enemy_.fight.duration+Time(15000):std::chrono::hours(8),speed_,{},true,true}; }
    std::vector<Rect> text_regions() const override { return !enemy_.amulet.empty() && !amulet_kept_?std::vector<Rect>{{.1,.1,.8,.85}}:std::vector<Rect>{}; }
    std::vector<game::FixedLabelQuery> fixed_labels() const override {
        return !enemy_.amulet.empty() && !amulet_kept_?std::vector<game::FixedLabelQuery>{{{.1,.1,.8,.85},{"Keep Old","Replace"}}}:std::vector<game::FixedLabelQuery>{};
    }
    WorkActivity activity() const noexcept override { return engaged_ && !complete_ && !session_finished_?WorkActivity::fighting:WorkActivity::other; }
    ActivityStatus activity_status() const override {
        const auto field=game::field_at(enemy_.field+".entry");
        return {engaged_?"Fighting":"Searching",field?enemy_.species+" · "+field->name:enemy_.id};
    }
    std::optional<automation::SharedActivity> shared_activity() const override {
        if (!enemy_.shared || !visible_ || complete_ || session_finished_) return {};
        return automation::SharedActivity{"enemies",{{"enemy",enemy_.species},{"field",enemy_.field}}};
    }
    void observe(const automation::View& view) override {
        observed_now_=view.context.now;
        if (view.context.now-last_frame_>Time(2000)) visible_=0;
        if (!started) { started=true; started_=view.context.now; }
        if (complete_ || session_finished_) return;
        if (enemy_.vicious) {
            const auto observed=game::read_vicious(*enemy_.vicious,view.context);
            if (!observed.has_value(view.context,Time(2000)) || observed.captured<=last_frame_) return;
            last_frame_=observed.captured;
            const auto current=[&](const auto& event) { return event && *event>=entered_ && *event<=view.context.now; };
            // A kill which predates this encounter's engagement belongs to somebody else.
            if (current(observed.value.engaged) && !engaged_) engaged_=*observed.value.engaged;
            visible_=engaged_?1:0;
            if (current(observed.value.defeated)) {
                if (engaged_ && *observed.value.defeated>*engaged_) {
                    complete_=true; completed_at_=view.context.now;
                    if (enemy_.progress) enemy_.progress->finish(true);
                } else {
                    session_finished_=unavailable_=true;
                    if (enemy_.progress) enemy_.progress->finish_unavailable(enemy_.cooldown);
                }
            }
            return;
        }
        const auto observed=game::read_enemies(*enemy_.observations,view.context,entered_);
        if (!observed || observed->visible.captured<=last_frame_) return;
        last_frame_=observed->visible.captured;
        const auto count=std::count_if(observed->visible.value.begin(),observed->visible.value.end(),[&](const auto& enemy) { return enemy.name==enemy_.species; });
        for (const auto& enemy:observed->visible.value)
            if (std::find(enemy_.avoid.begin(),enemy_.avoid.end(),enemy.name)!=enemy_.avoid.end())
                throw automation::ObservationUnavailable("Avoiding "+enemy.name+" during "+enemy_.id);
        const bool announced=std::any_of(observed->activity.begin(),observed->activity.end(),[&](const auto& activity) {
            return activity.name==enemy_.species && activity.field==enemy_.field;
        });
        const bool defeated=std::any_of(observed->recent_defeats.begin(),observed->recent_defeats.end(),[&](const auto& event) {
            return event.name==enemy_.species && event.seen>=entered_ && event.seen<=view.context.now;
        });
        if (enemy_.shared && !count && defeated) {
            if (engaged_) {
                complete_=true; completed_at_=view.context.now;
                if (enemy_.progress) enemy_.progress->finish(true);
            } else {
                session_finished_=unavailable_=true;
                if (enemy_.progress) enemy_.progress->finish_unavailable(enemy_.cooldown);
            }
            return;
        }
        // A field announcement can be another player's battle across the field.
        // Keep sweeping until the local health bar is visible before fighting.
        if (announced && enemy_.search.empty() && !engaged_) engaged_=view.context.now;
        visible_=unsigned(count);
        visible_peak_=std::max(visible_peak_,visible_);
        if (visible_ && !engaged_) engaged_=view.context.now;
        if (count==1 && enemy_.progress) for (const auto& enemy:observed->visible.value)
            if (enemy.name==enemy_.species && enemy.health_fraction) enemy_.progress->sample(*enemy.health_fraction,last_frame_);
        for (const auto& event:observed->recent_defeats)
            if (event.name==enemy_.species && event.seen>=entered_ && event.seen<=view.context.now) defeats_.insert(event.id);
        if (count) clear_since_.reset();
        else if ((!defeats_.empty() && defeats_.size()>=visible_peak_ && visible_peak_) || defeats_.size()>=enemy_.count) {
            if (!clear_since_) clear_since_=last_frame_;
            complete_=defeats_.size()>=enemy_.count || last_frame_-*clear_since_>=Time(2000);
            if (complete_ && !completed_at_) {
                completed_at_=view.context.now;
                if (enemy_.progress) enemy_.progress->finish(true);
            }
        }
    }
    automation::Step next(const automation::View& view) override {
        using namespace automation;
        // Search displacement is not combat displacement: fight around the discovery
        // point, then return there instead of crossing the entire field after a kill.
        if (!enemy_.search.empty() && !search_origin_ && (engaged_ || complete_ || session_finished_)) {
            // A defeat can arrive while still sweeping a bee activated by
            // somebody else. There is no fight displacement to undo then.
            x_=y_=0; search_origin_=true;
        }
        Observation<game::DialogButtons> buttons;
        if (!enemy_.amulet.empty() && !amulet_kept_ && view.observations) {
            const game::PromptLandmark title{{enemy_.amulet},{.1,.1,.8,.85},.7f,game::PromptMatch::phrase};
            static constexpr std::array<std::string_view,2> controls{"Keep Old","Replace"};
            const auto text=game::with_fixed_labels(view.context,view.observations->text,view.observations->fixed_labels,controls);
            buttons=game::read_dialog(view.context,text,title,{"Keep Old","Replace"});
            if (buttons.has_value(view.context,Time(2000),entered_)) {
                complete_=true; amulet_seen_=true;
                if (!completed_at_) {
                    completed_at_=view.context.now;
                    if (enemy_.progress) enemy_.progress->finish(true);
                }
            }
        }
        if (!complete_ && engaged_ && enemy_.session_limit>Time{} && view.context.now-*engaged_>=enemy_.session_limit)
            session_finished_=true;
        if (complete_) {
            if (!enemy_.amulet.empty() && !amulet_kept_) {
                if (!enemy_.keep_old) {
                    if (enemy_.amulet_optional && !amulet_seen_ && view.context.now-completed_at_.value_or(view.context.now)>=Time(3000)) amulet_kept_=true;
                    else if (amulet_seen_ && buttons.knowledge==Knowledge::absent && buttons.usable(view.context,Time(2000),completed_at_.value_or(entered_)))
                        amulet_kept_=true;
                    else return Wait{Time(250),"Choose your "+enemy_.amulet+" in Roblox"};
                } else if (buttons.has_value(view.context,Time(2000),entered_)) {
                    amulet_kept_=true;
                    return UseControl{{Knowledge::present,buttons.value.yes,buttons.captured,buttons.window,buttons.generation},0,"Keep existing amulet",Point{.85,.15}};
                } else {
                    if (view.context.now-completed_at_.value_or(view.context.now)<Time(enemy_.amulet_optional?3000:8000)) return Wait{Time(100),"Waiting for amulet"};
                    if (enemy_.amulet_optional) amulet_kept_=true;
                    else throw ObservationUnavailable("Amulet result was not readable");
                }
            }
        }
        if (complete_ || session_finished_) {
            if (std::abs(x_)>.001) { pending_=game::WalkLeg{std::abs(x_),x_>0?Key::left:Key::right,{}}; return Walk{*pending_,false}; }
            if (std::abs(y_)>.001) { pending_=game::WalkLeg{std::abs(y_),y_>0?Key::backward:Key::forward,{}}; return Walk{*pending_,false}; }
            if (!complete_ && !unavailable_ && enemy_.progress) enemy_.progress->finish(false,enemy_.revisit?enemy_.revisit():Time(std::chrono::minutes(30)));
            return Done{(complete_?"Defeated ":unavailable_?"Already defeated: ":"Finished visit to ")+enemy_.id};
        }
        if (enemy_.scheduled && !enemy_.scheduled(view.context)) throw ObservationUnavailable("Encounter is no longer available: "+enemy_.id);
        if (view.context.now-engaged_.value_or(started_)>=enemy_.fight.duration)
            throw ObservationUnavailable("Fight timed out: "+enemy_.id);
        if (!enemy_.search.empty() && (last_frame_<Time{} || view.context.now-last_frame_>Time(2000))) {
            if (view.context.now-std::max(started_,last_frame_)>=Time(5000))
                throw ObservationUnavailable("Vicious Bee observations are unavailable");
            return Wait{Time(100),"Waiting for enemy observations"};
        }
        if (enemy_.vicious && view.context.now-chat_at_>=Time(30000)) {
            chat_at_=view.context.now; return RunProgram{show_chat};
        }
        if (!engaged_ && defeats_.empty() && !enemy_.search.empty()) {
            if (enemy_.requested_field) {
                const auto field=enemy_.requested_field(view.context);
                if (!field.empty() && field!=enemy_.field) {
                    if (enemy_.progress) enemy_.progress->finish(false);
                    const auto definition=game::field_at(field+".entry");
                    return Done{"Vicious Bee reported in "+(definition?definition->name:field)};
                }
            }
            if (search_==enemy_.search.size()) {
                if (!search_finished_) search_finished_=view.context.now;
                if (last_frame_<*search_finished_) return Wait{Time(100),"Checking the end of the search"};
                if (enemy_.progress) enemy_.progress->finish(false,std::chrono::minutes(10));
                return Done{"No Vicious Bee in this field"};
            }
            return walk(enemy_.search[search_++],true);
        }
        if (!engaged_ && defeats_.empty() && view.context.now-started_>=Time(10000))
            throw ObservationUnavailable("No enemy found: "+enemy_.id);
        if (!engaged_ && defeats_.empty()) return Wait{Time(100),"Looking for "+enemy_.species};
        if (enemy_.stationary) return Hold{{Key::primary},Time(250),Point{.85,.15}};
        if ((enemy_.movement_radius>0 && std::hypot(x_,y_)>=enemy_.movement_radius)
            || view.context.now-moved_<enemy_.movement_interval) return Hold{{Key::primary},Time(250),Point{.85,.15}};
        moved_=view.context.now;
        return walk(enemy_.fight.legs[leg_++%enemy_.fight.legs.size()]);
    }
    void step_completed(const automation::View&) override {
        if (!pending_) return;
        const auto remaining=distance_?distance_->remaining_tiles():0.;
        const auto distance=(pending_->tiles-remaining)*(pending_->second?1/std::sqrt(2.):1.);
        // A stale capture pauses the leg. Resume its remainder once observations
        // recover instead of silently skipping that part of the search route.
        if (search_leg_ && !engaged_ && !complete_ && !session_finished_ && remaining>.001)
            enemy_.search[--search_].leg.tiles=remaining;
        distance_.reset(); search_leg_=false;
        const auto move=[&](Key key) {
            if (key==Key::left) x_-=distance; else if (key==Key::right) x_+=distance;
            else if (key==Key::forward) y_+=distance; else if (key==Key::backward) y_-=distance;
        };
        move(pending_->first); if (pending_->second) move(*pending_->second); pending_.reset();
    }
};
}
std::unique_ptr<automation::Program> encounter(const Enemy& enemy,double speed,Time entered) {
    return std::make_unique<Encounter>(enemy,speed,entered);
}
}
