#include "hive/game/movement/movement.hpp"
#include <algorithm>

namespace hive::game {
namespace {
void valid_speed(double speed) {
    if (!std::isfinite(speed) || speed<=0 || speed>1000) throw std::invalid_argument("Movement speed must be positive and at most 1000 studs/s");
}
bool direction(Key key) { return key==Key::forward || key==Key::backward || key==Key::left || key==Key::right; }
}
DistanceIntegrator::DistanceIntegrator(double tiles): target_studs_(tiles*4) {
    if (!std::isfinite(tiles) || tiles<=0 || tiles>10000) throw std::invalid_argument("Invalid walk distance");
}
void DistanceIntegrator::advance(Time moving_time,double before,double after) {
    valid_speed(before); valid_speed(after);
    if (moving_time<Time::zero() || moving_time>Time(250)) throw std::invalid_argument("Movement samples must be at most 250 ms apart");
    distance_+=0.5*(before+after)*moving_time.count()/1000.0;
}
double DistanceIntegrator::remaining_tiles() const { return std::max(0.0,(target_studs_-distance_)/4); }
bool DistanceIntegrator::complete() const { return distance_+1e-9>=target_studs_; }
MovementSpeed::MovementSpeed(double configured):configured_(configured) {
    if (!std::isfinite(configured) || configured<1 || configured>200) throw std::invalid_argument("Invalid configured movement speed");
}
void MovementSpeed::start(const ObservationContext& c) {
    context_=c; samples_={{c.now,configured_}}; observed_=Time(-1);
}
void MovementSpeed::observe(const ObservationContext& c,const Observation<double>& reading) {
    if (samples_.empty() || c.now<context_.now || c.generation!=context_.generation || c.window.id!=context_.window.id
        || c.window.process!=context_.window.process || c.window.bounds!=context_.window.bounds)
        throw std::runtime_error("Movement speed context changed");
    context_=c;
    if (!reading.has_value(c,Time(2000)) || reading.captured<observed_) return;
    if (!std::isfinite(reading.value) || reading.value<1 || reading.value>1000) throw std::runtime_error("Invalid observed movement speed");
    if (reading.captured==observed_) {
        if (reading.value!=samples_.back().speed) throw std::runtime_error("Conflicting movement speed observation");
        return;
    }
    observed_=reading.captured;
    const auto stamp=std::max(samples_.back().captured,reading.captured);
    if (stamp==samples_.back().captured) samples_.back().speed=reading.value;
    else samples_.push_back({stamp,reading.value});
    if (samples_.size()>2048) throw std::runtime_error("Movement speed observation limit exceeded");
}
double MovementSpeed::current() const {
    if (samples_.empty()) throw std::logic_error("Movement speed was not started");
    return samples_.back().speed;
}
void MovementSpeed::advance(DistanceIntegrator& distance,Time finished,Time duration) {
    if (samples_.empty() || duration<Time{} || duration>std::chrono::minutes(10) || finished>context_.now || finished-duration<samples_.front().captured)
        throw std::runtime_error("Movement execution interval is not supported");
    const auto integrate=[&](Time duration,double speed) {
        while (duration>Time{}) {
            const auto part=std::min(duration,Time(250));
            distance.advance(part,speed,speed); duration-=part;
        }
    };
    auto cursor=finished-duration;
    double speed=samples_.front().speed;
    for (const auto& sample : samples_) {
        if (sample.captured<=cursor) { speed=sample.speed; continue; }
        if (sample.captured>=finished) break;
        integrate(sample.captured-cursor,speed);
        cursor=sample.captured; speed=sample.speed;
    }
    integrate(finished-cursor,speed);
    while (samples_.size()>2 && samples_[1].captured<finished-Time(2000)) samples_.pop_front();
}
namespace {
class PacedWalk final : public ActionPacing {
    WalkLeg leg_;
    DistanceIntegrator distance_;
    std::shared_ptr<MovementSpeed> speed_;
    ObservationContext context_;
public:
    PacedWalk(WalkLeg leg,std::shared_ptr<MovementSpeed> speed,ObservationContext context)
        :leg_(leg),distance_(leg.tiles),speed_(std::move(speed)),context_(std::move(context)) {}
    Time next(Time from,Time to) override {
        context_.now=to; speed_->observe(context_,{});
        speed_->advance(distance_,to,to-from);
        return distance_.complete()?Time{}:movement_slice(leg_,distance_.remaining_tiles(),speed_->current()).duration;
    }
};
}
Action paced_walk(const WalkLeg& leg,std::shared_ptr<MovementSpeed> speed,ObservationContext context,bool tool) {
    if (!speed) throw std::invalid_argument("Walking needs a movement speed source");
    auto action=movement_slice(leg,leg.tiles,speed->current(),Time(100),tool);
    action.duration=Time(100);
    action.pacing=std::make_shared<PacedWalk>(leg,std::move(speed),std::move(context));
    return action;
}
Action movement_slice(const WalkLeg& leg,double remaining,double speed,Time checkpoint,bool tool) {
    valid_speed(speed);
    if (!direction(leg.first) || (leg.second && (!direction(*leg.second) || *leg.second==leg.first
        || ((leg.first==Key::forward || leg.first==Key::backward)==(*leg.second==Key::forward || *leg.second==Key::backward))))) throw std::invalid_argument("Walk keys must be cardinal or perpendicular");
    if (!std::isfinite(leg.tiles) || leg.tiles<=0 || leg.tiles>10000 || !std::isfinite(remaining) || remaining<=0 || remaining>leg.tiles || checkpoint<Time(1) || checkpoint>Time(250)) throw std::invalid_argument("Invalid movement slice");
    auto duration=Time(static_cast<Time::rep>(std::max(1.0,std::min(double(checkpoint.count()),std::ceil(remaining*4000/speed)))));
    Action action{"Walk",{leg.first},duration,{}};
    if (leg.second) action.held.push_back(*leg.second);
    if (leg.jump) action.held.push_back(Key::jump);
    if (tool) action.held.push_back(Key::primary);
    return action;
}
}
