#include "buffs.hpp"
#include "buff_vision.hpp"
#include <algorithm>

namespace hive::game {
namespace {
class BuffDetector final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        auto result=std::make_shared<BuffDetection>();
        const auto definitions=detail::report_buff_definitions();
        for (const auto& definition:definitions) result->buffs.push_back({std::string(definition.type.name),{}});
        const auto layout=detail::hud_buff_bar(input);
        if (!layout) return result;
        const auto scan=detail::scan_buffs(input,*layout,definitions);
        if (!scan.complete) return result;
        for (std::size_t i=0;i<definitions.size();++i) {
            if (!definitions[i].missing_is_unknown && !scan.ambiguous[i]) {
                result->buffs[i].active=false;result->buffs[i].stacks=0;
            }
            if(std::any_of(scan.matches.begin(),scan.matches.end(),[&](const auto& match){return match.definition==i;}))
                result->buffs[i]=detail::read_buff_candidates(input,*layout,definitions[i],scan.matches,i);
        }
        if (input.cancelled && input.cancelled()) for (auto& buff:result->buffs) {
            buff.active.reset();buff.stacks.reset();buff.value.reset();
        }
        return result;
    }
};
}
std::span<const BuffType> tracked_buffs() {
    static const auto types=[] {
        std::vector<BuffType> result;
        for (const auto& definition:detail::report_buff_definitions()) result.push_back(definition.type);
        return result;
    }();
    return types;
}
std::shared_ptr<const Detector> buff_detector() { return std::make_shared<BuffDetector>(); }
std::optional<BuffSnapshot> buff_snapshot(const std::shared_ptr<const DetectionSource>& source,const ObservationContext& context) {
    const auto result=source?source->read():std::nullopt;
    if (!result || !result->error.empty()
        || !Observation<bool>{Knowledge::present,true,result->context.now,result->context.window,result->context.generation}.has_value(context,Time(7000))) return {};
    if (auto value=std::dynamic_pointer_cast<const BuffDetection>(result->value)) return BuffSnapshot{std::move(value),result->context.now};
    return {};
}
}
