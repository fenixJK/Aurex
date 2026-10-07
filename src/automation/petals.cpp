#include "hive/automation/petals.hpp"
#include "hive/automation/shift_lock.hpp"
#include "hive/game/data/fields.hpp"
#include "hive/game/detection/menus.hpp"
#include <algorithm>

namespace hive::automation {
namespace {
double distance(Point a,Point b) { return std::hypot(a.x-b.x,a.y-b.y); }
class Petals final : public Program {
    enum class Stage { lock,prepare,travel,camera,point,select,forward,right,approach,gather,loot,home,convert,done };
    PetalPlan plan_;
    std::vector<game::WalkLeg> loot_;
    Stage stage_{Stage::lock};
    game::Bloom target_;
    Point origin_,forward_,right_;
    std::optional<ObservationContext> context_;
    Time minimum_{},last_{Time(-1)},deadline_{},search_until_{},pop_start_{},missing_since_{};
    unsigned missing_{},moves_{},loot_step_{};
    int sign_{1};
    std::string reason_;
    bool moved_{},calibrated_{},yield_{},full_{};
    double travelled_{};

    Step home(std::string reason,bool full=false) {
        reason_=std::move(reason); full_=full; stage_=Stage::home;
        return next_home();
    }
    Step next_home() { stage_=full_?Stage::convert:Stage::done; return RunProgram{plan_.return_to_hive}; }
    Step reselect() {
        stage_=Stage::select; missing_=0; travelled_=0; moves_=0;
        return Wait{Time(50),"Finding the bloom again"};
    }
    Step walk(double tiles,Key key) { moved_=true; return Walk{{tiles,key,{}}}; }
    const game::Bloom* match(const game::Blooms& blooms,Point expected,double range) const {
        const game::Bloom* found=nullptr; double best=range,next=range;
        for (const auto& bloom:blooms.flowers) {
            if (bloom.radius<target_.radius*.65 || bloom.radius>target_.radius*1.5) continue;
            const auto error=distance(bloom.center,expected);
            if (error<best) { next=best; best=error; found=&bloom; }
            else next=std::min(next,error);
        }
        return found && next-best>.01?found:nullptr;
    }
    Point offset(Point point) const {
        const double x=.5-point.x,y=.5-point.y,det=forward_.x*right_.y-forward_.y*right_.x;
        return {(x*right_.y-y*right_.x)/det,(forward_.x*y-forward_.y*x)/det};
    }
public:
    explicit Petals(PetalPlan plan):plan_(std::move(plan)),loot_(movement::snapshot(plan_.movement)->legs("petal-loot",{{"field",plan_.field}})) {
        if (!game::field_at(plan_.field) || !plan_.source || !plan_.prepare || !plan_.return_to_hive
            || !std::isfinite(plan_.speed) || plan_.speed<8 || plan_.speed>200
            || plan_.duration<Time(1000) || plan_.duration>std::chrono::minutes(10)
            || unsigned(plan_.color)>unsigned(game::PetalColor::red)) throw std::invalid_argument("Invalid petal collection plan");
    }
    RunConfig config() const override { return {plan_.duration+std::chrono::minutes(8),plan_.speed,{},true,true}; }
    WorkActivity activity() const noexcept override { return stage_>=Stage::select && stage_<=Stage::loot?WorkActivity::gathering:WorkActivity::other; }
    std::string field() const override { return game::field_at(plan_.field)->name; }
    ActivityStatus activity_status() const override {
        if (stage_==Stage::home || stage_==Stage::convert || stage_==Stage::done) return {"Returning to hive",reason_};
        return {stage_==Stage::loot?"Collecting petals":"Collecting blooms",field()};
    }
    bool request_yield() override { yield_=true; return true; }
    Step next(const View& view) override {
        const auto& c=view.context;
        if (stage_==Stage::home) return next_home();
        if (stage_==Stage::convert) { stage_=Stage::done; return Convert{}; }
        if (stage_==Stage::done) return Done{reason_};
        if (yield_) return home("Petal collection yielded");
        if (stage_==Stage::lock) { stage_=Stage::prepare; return RunProgram{[] { return set_shift_lock(false); }}; }
        if (stage_==Stage::prepare) { stage_=Stage::travel; return RunProgram{plan_.prepare}; }
        if (stage_==Stage::travel) { stage_=Stage::camera; return Navigate{plan_.field}; }
        if (stage_==Stage::camera) {
            stage_=Stage::point; moved_=true; return MoveCamera{{Key::pitch_up,4}};
        }
        if (stage_==Stage::point) {
            stage_=Stage::select; return Hold{{},Time(1),game::pointer_rest(c,view.observations?*view.observations:game::State{})};
        }
        if (!context_) { context_=c; deadline_=c.now+plan_.duration; search_until_=c.now+Time(60000); }
        if (context_->generation!=c.generation || context_->window.id!=c.window.id || context_->window.process!=c.window.process
            || context_->window.bounds!=c.window.bounds) return home("Bloom view changed; checking quest progress");
        if (plan_.complete && plan_.complete(view)) return home("Petal objective complete");
        if (view.backpack_percent && *view.backpack_percent>=95) return home("Backpack full during petal collection",true);
        if (c.now>=deadline_ && stage_!=Stage::loot) return home("Petal visit finished; checking remaining objectives");
        if (stage_==Stage::loot) {
            if (loot_step_<loot_.size()) { moved_=true; return Walk{loot_[loot_step_++]}; }
            stage_=Stage::select; search_until_=c.now+Time(60000); travelled_=0; moves_=0;
        }
        const auto reading=game::read_blooms(*plan_.source,c);
        if (!reading.has_value(c,Time(1000),minimum_) || reading.captured<=last_) {
            if (c.now>std::max(minimum_,last_)+Time(10000)) return home("Bloom view unavailable; checking quest progress");
            if (!reading.has_value(c,Time(1000),minimum_)) { missing_=0; missing_since_=Time{}; }
            return Wait{Time(50),"Waiting for a fresh bloom view"};
        }
        last_=reading.captured;
        const auto& blooms=reading.value;
        if (stage_==Stage::select) {
            if (c.now>=search_until_) return home("No reachable blooms found in this field");
            const game::Bloom* best=nullptr; double score=10;
            for (const auto& bloom:blooms.flowers) {
                const double value=distance(bloom.center,{.5,.5})/(1+2*bloom.colors[unsigned(plan_.color)]);
                if (value<score) { best=&bloom; score=value; }
            }
            if (!best) return Hold{{Key::primary},Time(250)};
            target_=*best; missing_=0;
            search_until_=c.now+Time(60000);
            if (calibrated_) { stage_=Stage::approach; return Wait{Time(1),"Approaching bloom"}; }
            origin_=target_.center; sign_=origin_.y<.5?1:-1;
            stage_=Stage::forward; return walk(1.5,sign_>0?Key::forward:Key::backward);
        }
        if (stage_==Stage::forward || stage_==Stage::right) {
            const auto* found=match(blooms,origin_,.20);
            if (!found) return reselect();
            const Point vector{(found->center.x-origin_.x)/(1.5*sign_),(found->center.y-origin_.y)/(1.5*sign_)};
            if (distance(vector,{})<.0025 || distance(vector,{})>.10) return reselect();
            target_=*found;
            if (stage_==Stage::forward) {
                forward_=vector; origin_=target_.center; sign_=origin_.x>.5?1:-1;
                stage_=Stage::right; return walk(1.5,sign_>0?Key::right:Key::left);
            }
            right_=vector;
            const double area=std::abs(forward_.x*right_.y-forward_.y*right_.x);
            if (area<.5*distance(forward_,{})*distance(right_,{})) return reselect();
            calibrated_=true; stage_=Stage::approach;
        }
        if (stage_==Stage::approach) {
            const auto* found=match(blooms,target_.center,.08);
            if (!found) {
                if (++missing_>=3) return reselect();
                return Wait{Time(50),"Rechecking bloom target"};
            }
            missing_=0; target_=*found;
            const auto delta=offset(target_.center);
            const double error=std::hypot(delta.x,delta.y);
            if (error<=1.2) { stage_=Stage::gather; pop_start_=c.now; }
            else {
                if (++moves_>16 || travelled_>=36) { calibrated_=false; return reselect(); }
                const bool diagonal=std::min(std::abs(delta.x),std::abs(delta.y))>1.;
                const bool forward=std::abs(delta.x)>std::abs(delta.y);
                const double component=forward?delta.x:delta.y;
                const double tiles=std::min(8.,diagonal?std::min(std::abs(delta.x),std::abs(delta.y))*std::sqrt(2.):std::abs(component));
                const double along=tiles/(diagonal?std::sqrt(2.):1.);
                const double f=diagonal || forward?std::copysign(along,delta.x):0.;
                const double r=diagonal || !forward?std::copysign(along,delta.y):0.;
                target_.center.x+=forward_.x*f+right_.x*r;
                target_.center.y+=forward_.y*f+right_.y*r;
                travelled_+=tiles; moved_=true;
                const auto key=forward?(component>0?Key::forward:Key::backward):(component>0?Key::right:Key::left);
                const auto second=diagonal?std::optional(forward?(delta.y>0?Key::right:Key::left):(delta.x>0?Key::forward:Key::backward)):std::nullopt;
                return Walk{{tiles,key,second},true};
            }
        }
        if (stage_==Stage::gather) {
            const auto* found=match(blooms,target_.center,std::max(.025,target_.radius));
            if (found) {
                target_=*found; missing_=0; missing_since_=Time{};
                if (c.now-pop_start_>=Time(20000)) { calibrated_=false; return reselect(); }
            } else {
                if (!missing_++) missing_since_=reading.captured;
                if (missing_>=3 && reading.captured-missing_since_>=Time(500)) {
                    stage_=Stage::loot; loot_step_=0; return Wait{Time(1),"Bloom disappeared; collecting scattered petals"};
                }
            }
            return Hold{{Key::primary},Time(250)};
        }
        return Wait{};
    }
    void step_completed(const View& view) override {
        if (moved_) { minimum_=view.context.now; moved_=false; }
    }
};
}
std::unique_ptr<Program> collect_petals(PetalPlan plan) { return std::make_unique<Petals>(std::move(plan)); }
}
