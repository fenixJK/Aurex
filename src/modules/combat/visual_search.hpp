#pragma once
#include "core/runtime/program.hpp"
#include "game/detection/vicious.hpp"

namespace hive::features::combat {
// Approach a visual candidate in a settled overhead view. Movement is measured
// from successive frames, so resolution and camera zoom do not define tile size.
class VisualSearch {
    unsigned stage_{},moves_{},empty_{};
    Time started_{Time(-1)},minimum_{},last_{Time(-1)};
    Point target_{},origin_{},forward_{},right_{};
    int sign_{1};
    bool moved_{};
public:
    automation::Step next(const automation::View&,const Observation<game::ViciousSignals>&);
    void completed(Time now,bool interrupted=false) {
        if (moved_) { minimum_=now+Time(100); moved_=false; }
        if (interrupted) stage_=0;
    }
};
}
