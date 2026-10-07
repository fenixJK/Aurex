#pragma once
#include "hive/core/model.hpp"
#include <memory>

namespace hive::game {
class ScreenReferenceSource {
public:
    virtual ~ScreenReferenceSource()=default;
    virtual Observation<Point> read(const ObservationContext& context) const=0;
};
}
