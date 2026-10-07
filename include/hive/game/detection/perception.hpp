#pragma once
#include "state.hpp"
#include <array>

namespace hive::game {
Reading<std::uint64_t> read_honey(const Frame& frame,const std::vector<TextLine>& lines,std::uint64_t generation);
Reading<double> read_backpack_percent(const Frame& frame,std::uint64_t generation);
std::optional<std::array<Rect,2>> resource_hud_regions(const Frame& frame);
State read_hud(const Frame& frame,const std::vector<TextLine>& lines,std::uint64_t generation);
std::vector<Rect> hud_text_regions(bool in_game);
}
