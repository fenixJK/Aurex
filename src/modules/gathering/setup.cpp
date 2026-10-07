#include "hive/modules/gathering/setup.hpp"
#include "defaults.hpp"
#include "hive/automation/recenter.hpp"
#include "hive/automation/sprinklers.hpp"
#include "hive/automation/field_routes.hpp"
#include "hive/game/data/fields.hpp"
#include <algorithm>

namespace hive::features::gathering {
Services field_services(const game::RouteNetwork& routes,double speed,const std::vector<PatternPreset>& patterns,
    std::shared_ptr<const game::ScreenReferenceSource> reference,automation::FieldReturnOptions returns) {
    Services service;
    service.description="Gather in your selected fields and convert at the hive. Quest and nectar goals can adjust the field choice.";
    if (!patterns.empty()) service.controls=PatternControls{patterns,"Square"};
    if (std::none_of(patterns.begin(),patterns.end(),[](const auto& pattern) { return pattern.name=="Square"; }) && !patterns.empty())
        service.controls->initial=patterns.front().name;
    service.sprinklers=SprinklerControls{[speed,movement=returns.movement](const auto& field,const auto& equipment,unsigned slot) {
        return automation::sprinkler_setup(field.preference.field,equipment,slot,speed,field.sprinkler_forward,field.sprinkler_lateral,field.start_offset,movement);
    }};
    if (reference) service.sprinklers->recenter=[source=reference,speed] {
        return automation::recenter(source,speed);
    };
    service.plan.hive="hive.proximity"; service.plan.config.speed=speed;
    std::set<std::string> added;
    for (const auto& route : routes.routes()) {
        const auto* definition=game::field_at(route.to);
        if (route.from!=service.plan.hive || !definition) continue;
        const bool walking_return=std::any_of(routes.routes().begin(),routes.routes().end(),
            [&](const auto& candidate) { return candidate.from==route.to && candidate.to==service.plan.hive; });
        if (!walking_return || !added.insert(definition->id).second) continue;
        const auto id=definition->id;
        FieldPlan field;
        field.preference={definition->name,definition->location(),service.plan.fields.empty()};
        field.pattern={{},std::chrono::minutes(10),95,{},"Gathering"};
        if (const auto* defaults=defaults_for(definition->name)) {
            field.rotation=game::heading_move(0,defaults->heading);
            field.start_offset=defaults->start; field.shift_lock=false;
        }
        if (service.controls) {
            const auto& presets=service.controls->presets;
            const auto initial=std::find_if(presets.begin(),presets.end(),[&](const auto& p) { return p.name==service.controls->initial; });
            field.strategy=initial->strategy(service.controls->width);
        } else field.strategy=[]() -> std::unique_ptr<automation::Program> { throw std::runtime_error("No valid Lua patterns are available"); };
        field.return_setup=[id,speed,returns] { return automation::return_from_field(id,speed,0,returns); };
        service.plan.fields.push_back(std::move(field));
    }
    return service;
}
}
