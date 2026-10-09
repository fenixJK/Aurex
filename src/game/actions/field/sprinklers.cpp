#include "sprinklers.hpp"
#include "game/data/fields.hpp"
#include <algorithm>

namespace hive::automation {
std::vector<Step> sprinkler_setup(const std::string& field,const std::string& equipment,unsigned slot,double speed,double forward_spacing,double lateral_spacing,Point start_offset,movement::Source source) {
    const auto fields=game::fields();
    const auto types=game::sprinkler_equipment();
    const auto type=std::find_if(types.begin(),types.end(),[&](const auto& entry) { return entry.name==equipment; });
    if (type==types.end() || std::none_of(fields.begin(),fields.end(),[&](const auto& entry) { return entry.name==field; })
        || !std::isfinite(forward_spacing) || forward_spacing<.5 || forward_spacing>15
        || !std::isfinite(lateral_spacing) || lateral_spacing<.5 || lateral_spacing>15
        || !std::isfinite(start_offset.x) || !std::isfinite(start_offset.y)
        || slot<1 || slot>7 || !std::isfinite(speed) || speed<8 || speed>200)
        throw std::invalid_argument("Invalid sprinkler setup");
    std::vector<Step> result;
    const auto sequence=movement::snapshot(source)->sequence("sprinklers",{{"field",field},{"count",int(type->count)},
        {"forward_spacing",forward_spacing},{"lateral_spacing",lateral_spacing},{"start_side",start_offset.x},{"start_forward",start_offset.y}},speed);
    unsigned placed=0;
    for (const auto& step:sequence) {
        if (const auto* marker=std::get_if<movement::Marker>(&step);marker && marker->name=="place") {
            if (++placed>type->count) throw std::runtime_error("Sprinkler layout places too many sprinklers");
            result.push_back(PlaceSprinkler{{field,"sprinkler-"+std::to_string(placed),slot,type->jump?Time(200):Time{},
                Time(15000),workflows::SprinklerCompletion::attempted},{}});
        } else if (const auto* path=std::get_if<workflows::CheckpointPlan>(&step);path && path->completion==workflows::CheckpointCompletion::movement)
            result.push_back(FollowPath{*path});
        else throw std::runtime_error("Unsupported sprinkler layout step");
    }
    if (placed!=type->count) throw std::runtime_error("Sprinkler layout does not match equipped sprinklers");
    return result;
}
}
