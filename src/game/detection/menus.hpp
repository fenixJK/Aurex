#pragma once
#include "detection.hpp"
#include "state.hpp"
#include <span>

namespace hive::game {
std::optional<Rect> sidebar_region(const DetectionInput& input,MenuTab tab);
Point pointer_rest(const ObservationContext& context,const State& state);
std::vector<MenuControl> menu_controls(const DetectionInput& input,std::optional<Rect> search={},std::span<const MenuControl> hints={});
}
