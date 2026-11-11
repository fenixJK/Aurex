#include "movement_buffs.hpp"
#include "buff_vision.hpp"
#include <algorithm>

namespace hive::game {
std::span<const BuffType> tracked_movement_buffs() {
    static const auto types=[] {
        std::vector<BuffType> result;
        for (const auto& definition:detail::movement_buff_definitions()) result.push_back(definition.type);
        return result;
    }();
    return types;
}
MovementBuffCandidates detail::movement_buffs_at(const DetectionInput& input,const BuffBar& layout) {
    MovementBuffCandidates result;
    if (input.text.empty() && layout.bottom>input.frame.viewport_height()*.35) return result;
    const double top=layout.bottom-layout.pitch,left=std::max(0.,layout.origin);
    if (top<0 || layout.bottom>input.frame.height || input.frame.width-left<layout.pitch) return result;
    const auto definitions=movement_buff_definitions();
    const auto scan=scan_buffs(input,layout,definitions);
    if (!scan.complete) return result;
    int first=0,last=0;
    for (const auto& definition:definitions) for (const auto& appearance:definition.appearances) {
        first=std::min(first,appearance.first_offset);last=std::max(last,appearance.last_offset);
    }
    const auto search_top=std::max(0.,top+first*layout.scale);
    result.bar={left,search_top,input.frame.width-left,std::min(double(input.frame.height),layout.bottom+last*layout.scale)-search_top};
    for (const auto& match:scan.matches) {
        const auto& definition=definitions[match.definition];
        const auto value=read_buff_value(input,layout,definition,match.cell);
        result.candidates.push_back({value.name,match.cell,definition.value_kind==BuffValueKind::stacks?value.stacks:std::nullopt});
    }
    if (input.cancelled && input.cancelled()) return {};
    // Ambiguous artwork is an unknown reading, never evidence of base speed.
    result.layout_known=std::none_of(scan.ambiguous.begin(),scan.ambiguous.end(),[](bool value){return value;});
    return result;
}
}
