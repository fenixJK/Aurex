#pragma once
#include "core/runtime/model.hpp"
#include <limits>
#include <span>

namespace hive::game {
template<class T,class Position,class Accept>
const T* nearest_target(std::span<const T> candidates,Point expected,double range,double separation,Position position,Accept accept) {
    const T* selected=nullptr;
    double nearest=range,second=std::numeric_limits<double>::infinity();
    for (const auto& candidate:candidates) {
        if (!accept(candidate)) continue;
        const auto point=position(candidate);
        const auto distance=std::hypot(point.x-expected.x,point.y-expected.y);
        if (!std::isfinite(distance)) continue;
        if (distance<nearest) { second=selected?nearest:second; nearest=distance; selected=&candidate; }
        else if (distance<second) second=distance;
    }
    return selected && second-nearest>=separation?selected:nullptr;
}
}
