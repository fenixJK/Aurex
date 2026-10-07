#pragma once
#include "hive/game/detection/detection.hpp"
#include "hive/game/detection/movement_buffs.hpp"
namespace hive::game::detail {
struct BuffBar { double scale,pitch,origin,bottom; bool menu_layout{}; };
MovementBuffCandidates movement_buffs_at(const DetectionInput&,const BuffBar&);
int nectar_color(const Frame&,int x,int y,int tolerance=6);
std::optional<BuffBar> buff_bar(const DetectionInput& input);
std::optional<BuffBar> hud_buff_bar(const DetectionInput& input);
}
