#include "plan.hpp"
#include <algorithm>
#include "game/detection/dialog.hpp"

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
    bool started{},complete_{},amulet_kept_{},amulet_seen_{},session_finished_{},unavailable_{};
    std::optional<game::WalkLeg> pending_;
public:
    Encounter(Enemy enemy,double speed,Time entered):enemy_(std::move(enemy)),speed_(speed),entered_(entered) {
        if (!enemy_.observations || enemy_.count<1 || enemy_.count>8 || enemy_.fight.legs.empty())
            throw std::invalid_argument("Encounter needs observations and a movement plan");
        if (enemy_.progress) enemy_.progress->begin(enemy_.maximum_health,enemy_.initial_health);
    }
    automation::RunConfig config() const override { return {enemy_.keep_old?enemy_.fight.duration+Time(15000):std::chrono::hours(8),speed_,{},true,true}; }
    std::vector<Rect> text_regions() const override { return !enemy_.amulet.empty() && !amulet_kept_?std::vector<Rect>{{.1,.1,.8,.85}}:std::vector<Rect>{}; }
    WorkActivity activity() const noexcept override { return engaged_ && !complete_ && !session_finished_?WorkActivity::fighting:WorkActivity::other; }
    ActivityStatus activity_status() const override { return {"Fighting",enemy_.id}; }
    std::optional<automation::SharedActivity> shared_activity() const override {
        if (!enemy_.shared || !visible_ || complete_ || session_finished_) return {};
        return automation::SharedActivity{"enemies",{{"enemy",enemy_.species},{"field",enemy_.field}}};
    }
    void observe(const automation::View& view) override {
        if (view.context.now-last_frame_>Time(2000)) visible_=0;
        if (!started) { started=true; started_=view.context.now; }
        if (complete_ || session_finished_) return;
        const auto packet=enemy_.observations->read();
        if (!packet || !packet->error.empty()) return;
        const Observation<bool> envelope{Knowledge::present,true,packet->context.now,packet->context.window,packet->context.generation};
        if (!envelope.has_value(view.context,Time(2000))) return;
        const auto observed=std::dynamic_pointer_cast<const game::EnemiesDetection>(packet->value);
        if (!observed || !observed->visible.has_value(view.context,Time(2000),entered_)
            || observed->visible.captured<=last_frame_) return;
        last_frame_=observed->visible.captured;
        const auto count=std::count_if(observed->visible.value.begin(),observed->visible.value.end(),[&](const auto& enemy) { return enemy.name==enemy_.species; });
        for (const auto& enemy:observed->visible.value)
            if (std::find(enemy_.avoid.begin(),enemy_.avoid.end(),enemy.name)!=enemy_.avoid.end())
                throw automation::ObservationUnavailable("Avoiding "+enemy.name+" during "+enemy_.id);
        const bool announced=std::any_of(observed->activity.begin(),observed->activity.end(),[&](const auto& activity) {
            return activity.name==enemy_.species && activity.field==enemy_.field;
        });
        const auto defeat=observed->defeats.find(enemy_.species);
        if (enemy_.shared && !count && defeat!=observed->defeats.end()
            && defeat->second.has_value(view.context,Time(2000),entered_) && defeat->second.value) {
            if (engaged_) {
                complete_=true; completed_at_=view.context.now;
                if (enemy_.progress) enemy_.progress->finish(true);
            } else {
                session_finished_=unavailable_=true;
                if (enemy_.progress) enemy_.progress->finish_unavailable(enemy_.cooldown);
            }
            return;
        }
        if (announced && !engaged_) engaged_=view.context.now;
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
        Observation<game::DialogButtons> buttons;
        if (!enemy_.amulet.empty() && !amulet_kept_ && view.observations) {
            const game::PromptLandmark title{{enemy_.amulet},{.1,.1,.8,.85},.7f,game::PromptMatch::phrase};
            buttons=game::read_dialog(view.context,view.observations->text,title,{"Keep Old","Replace"});
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
        if (view.context.now-started_>=enemy_.fight.duration)
            throw ObservationUnavailable("Fight timed out: "+enemy_.id);
        if (!engaged_ && defeats_.empty() && !enemy_.search.empty()) {
            if (search_==enemy_.search.size()) {
                if (enemy_.progress) enemy_.progress->finish(false,std::chrono::minutes(10));
                return Done{"No Vicious Bee in this field"};
            }
            auto step=enemy_.search[search_++]; pending_=step.leg; return step;
        }
        if (!engaged_ && defeats_.empty() && view.context.now-started_>=Time(10000))
            throw ObservationUnavailable("No enemy found: "+enemy_.id);
        if (!engaged_ && defeats_.empty()) return Wait{Time(100),"Looking for "+enemy_.species};
        if (enemy_.stationary) return Hold{{Key::primary},Time(250),Point{.85,.15}};
        if ((enemy_.movement_radius>0 && std::hypot(x_,y_)>=enemy_.movement_radius)
            || view.context.now-moved_<enemy_.movement_interval) return Hold{{Key::primary},Time(250),Point{.85,.15}};
        moved_=view.context.now;
        const auto& step=enemy_.fight.legs[leg_++%enemy_.fight.legs.size()]; pending_=step.leg;
        return step;
    }
    void step_completed(const automation::View&) override {
        if (!pending_) return;
        const auto distance=pending_->tiles*(pending_->second?1/std::sqrt(2.):1.);
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
