#include "visual_search.hpp"
#include "game/detection/nearest_target.hpp"

namespace hive::features::combat {
automation::Step VisualSearch::next(const automation::View& view,const Observation<game::ViciousSignals>& reading) {
    using namespace automation;
    const auto now=view.context.now;
    if (started_<Time{}) started_=minimum_=now;
    if (now-started_>Time(30000) || moves_>=20) return Done{"Vicious Bee candidate could not be confirmed"};
    if (!reading.has_value(view.context,Time(1000),minimum_) || !reading.value.world_visible || reading.captured<=last_) {
        if (now-std::max(minimum_,last_)>Time(5000)) throw ObservationUnavailable("Vicious Bee search view is unavailable");
        return Wait{Time(50),"Reading the field"};
    }
    last_=reading.captured;
    const auto& candidates=reading.value.candidates;
    auto distance=[](Point a,Point b) { return std::hypot(a.x-b.x,a.y-b.y); };
    auto walk=[&](double tiles,Key key) -> Step { moved_=true;++moves_;return Walk{{tiles,key,{}},true}; };
    const game::ViciousCandidate* found=nullptr;
    if (stage_==0) {
        double best=2;
        for (const auto& c:candidates) {
            const auto d=distance(c.center,{.5,.5});
            // The avatar's own accessories/bees are not a distant field target.
            if (d>.07 && d<best) { best=d;found=&c; }
        }
        if (!found) {
            if (++empty_>=3 && now-started_>=Time(1000)) return Done{"No Vicious Bee candidate in this view"};
            return Wait{Time(100),"Checking the field"};
        }
        empty_=0;origin_=target_=found->center;sign_=origin_.y<.5?1:-1;stage_=1;
        return walk(1.5,sign_>0?Key::forward:Key::backward);
    }
    found=game::nearest_target<game::ViciousCandidate>(candidates,target_,stage_<3?.20:.10,.015,
        [](const auto& c) { return c.center; },[](const auto&) { return true; });
    if (!found) { stage_=0;return Wait{Time(100),"Finding the Vicious Bee candidate again"}; }
    target_=found->center;
    if (stage_<3) {
        const Point delta{(target_.x-origin_.x)/(1.5*sign_),(target_.y-origin_.y)/(1.5*sign_)};
        const auto length=distance(delta,{});
        if (length<.0025 || length>.10) { stage_=0;return Wait{Time(100),"Rechecking candidate movement"}; }
        if (stage_==1) {
            forward_=delta;origin_=target_;sign_=target_.x>.5?1:-1;stage_=2;
            return walk(1.5,sign_>0?Key::right:Key::left);
        }
        right_=delta;stage_=3;
    }
    const double det=forward_.x*right_.y-forward_.y*right_.x;
    if (std::abs(det)<.5*distance(forward_,{})*distance(right_,{})) {
        stage_=0;return Wait{Time(100),"Rechecking candidate direction"};
    }
    const double dx=.5-target_.x,dy=.5-target_.y;
    const double f=(dx*right_.y-dy*right_.x)/det,r=(dy*forward_.x-dx*forward_.y)/det;
    if (std::hypot(f,r)<1.2) return Hold{{Key::primary},Time(200)};
    const bool forward=std::abs(f)>std::abs(r);
    const double value=forward?f:r,tiles=std::min(4.,std::abs(value));
    const auto vector=forward?forward_:right_;
    target_.x+=vector.x*std::copysign(tiles,value);target_.y+=vector.y*std::copysign(tiles,value);
    return walk(tiles,forward?(value>0?Key::forward:Key::backward):(value>0?Key::right:Key::left));
}
}
