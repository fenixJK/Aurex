#pragma once
#include "scripts.hpp"
#include "game/detection/screen_reference.hpp"
#include "game/actions/travel/field_routes.hpp"

namespace hive::features::gathering {
struct Services {
    Plan plan;
    std::shared_ptr<const game::FieldPolicy> policy;
    std::string description;
    std::optional<PatternControls> controls;
    std::optional<SprinklerControls> sprinklers;
};
Services field_services(const game::RouteNetwork& routes,double speed,const std::vector<PatternPreset>& patterns,
    std::shared_ptr<const game::ScreenReferenceSource> reference={},automation::FieldReturnOptions returns={});
}
