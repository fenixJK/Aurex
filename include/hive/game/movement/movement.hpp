#pragma once
#include "hive/core/model.hpp"
#include <deque>
#include <memory>

namespace hive::game {
class MovementSpeedSource {
public:
    virtual ~MovementSpeedSource()=default;
    virtual void reset() const=0;
    virtual Observation<double> read(const ObservationContext& context,const Observation<double>& measured) const=0;
};
struct WalkLeg {
    double tiles{};
    Key first{Key::forward};
    std::optional<Key> second;
    bool jump{};
};
class DistanceIntegrator {
public:
    explicit DistanceIntegrator(double tiles);
    void advance(Time moving_time,double speed_before,double speed_after);
    double remaining_tiles() const;
    bool complete() const;
private:
    double target_studs_{},distance_{};
};
class MovementSpeed {
public:
    explicit MovementSpeed(double configured);
    void start(const ObservationContext& context);
    void observe(const ObservationContext& context,const Observation<double>& reading);
    double current() const;
    void advance(DistanceIntegrator& distance,Time finished,Time duration);
private:
    struct Sample { Time captured; double speed; };
    double configured_;
    ObservationContext context_;
    std::deque<Sample> samples_;
    Time observed_{Time(-1)};
};
Action movement_slice(const WalkLeg& leg,double remaining_tiles,double speed,Time checkpoint=Time(100),bool use_tool=false);
Action paced_walk(const WalkLeg& leg,std::shared_ptr<MovementSpeed> speed,ObservationContext context,bool use_tool=false);
}
