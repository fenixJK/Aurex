#pragma once
#include "program.hpp"
#include "hive/scripting/movement.hpp"

namespace hive::automation {
std::vector<Step> sprinkler_setup(const std::string& field,const std::string& equipment,unsigned slot,double speed,double forward_spacing=4.5,double lateral_spacing=4.5,Point start_offset={},movement::Source movement={});
}
