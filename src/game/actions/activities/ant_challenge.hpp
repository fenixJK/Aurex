#pragma once
#include "core/scripting/movement.hpp"
#include "core/runtime/program.hpp"
#include "game/actions/travel/navigation.hpp"

namespace hive::automation {
std::shared_ptr<const workflows::RouteCatalog> ant_routes(double speed,std::shared_ptr<const movement::Recipes> recipes={});
std::unique_ptr<Program> ant_challenge(ProgramFactory prepare,ProgramFactory home,double speed,movement::Source movement={},std::function<void(const ObservationContext&)> unavailable={});
}
