#pragma once
#include "core/runtime/model.hpp"
#include <memory>

namespace hive::game {
class ScreenReferenceSource {
public:
    virtual ~ScreenReferenceSource()=default;
    virtual Observation<Point> read(const ObservationContext& context,std::optional<Point> expected={}) const=0;
};
}
