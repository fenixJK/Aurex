#pragma once
#include "core/runtime/program.hpp"

namespace hive::features::gathering {
struct TimeExtension {
    Time limit{};
    std::function<bool(const automation::View&)> active;
};
std::unique_ptr<automation::Program> bounded_pattern(automation::ProgramFactory create,
    automation::RunConfig limits,std::function<void(unsigned)> completed,automation::ProgramFactory between_cycles={},std::function<bool()> yield={},std::function<unsigned(const automation::View&)> hotbar={},TimeExtension extension={},std::shared_ptr<const game::DetectionSource> buffs={});
}
