#pragma once
#include "hive/core/model.hpp"
#include "hive/core/statistics.hpp"
#include <span>

namespace hive::automation {
struct HotbarContext {
    ObservationContext observation;
    WorkActivity activity{WorkActivity::other};
    std::string field;
    std::optional<double> backpack;
};
class HotbarSource {
public:
    virtual ~HotbarSource()=default;
    virtual bool needs_backpack() const { return false; }
    virtual unsigned take(const HotbarContext& context)=0;
};
bool attach_hotbar(std::span<Action> actions,HotbarSource& source,const HotbarContext& context);
}
