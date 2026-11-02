#pragma once
#include "core/runtime/program.hpp"

namespace hive::automation {
struct TravelArrival {
    ProgramFactory prepare;
    // Startup work is constructed before Roblox launches. Check the join
    // marker when running, not when creating the routine.
    std::function<bool()> pending;
};
std::unique_ptr<Program> prepare_travel(ProgramFactory movement,ProgramFactory hive,double speed,bool convert_backpack=true,TravelArrival arrival={});
}
