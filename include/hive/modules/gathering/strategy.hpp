#pragma once
#include "hive/automation/program.hpp"

namespace hive::features::gathering {
std::unique_ptr<automation::Program> bounded_pattern(automation::ProgramFactory create,
    automation::RunConfig limits,std::function<void(unsigned)> completed,automation::ProgramFactory between_cycles={},std::function<bool()> yield={},std::function<unsigned(const automation::View&)> hotbar={});
}
