#pragma once
#include "core/runtime/program.hpp"
#include "core/scripting/movement.hpp"
#include "game/detection/blooms.hpp"

namespace hive::automation {
struct PetalPlan {
    std::string field;
    game::PetalColor color{game::PetalColor::any};
    std::shared_ptr<const game::DetectionSource> source;
    ProgramFactory prepare,return_to_hive;
    double speed{28};
    Time duration{300000};
    movement::Source movement;
    std::function<bool(const View&)> complete;
};
std::unique_ptr<Program> collect_petals(PetalPlan plan);
}
