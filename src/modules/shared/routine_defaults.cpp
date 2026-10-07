#include "hive/modules/shared/routine.hpp"
#include "hive/policies/defaults.hpp"

namespace hive::features::routine {
using namespace automation;
std::unique_ptr<Program> routine_program(Routine routine) {
    return routine_program(std::move(routine),std::make_unique<policies::FairActivityPolicy>());
}
}
