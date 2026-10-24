#pragma once
#include "state.hpp"
#include <array>

namespace hive::game {
struct DetectionInput;
struct HudFeatures {
    std::vector<MenuControl> menus;
    Reading<Point> open_menu;
    Reading<double> backpack;
    Reading<InteractionBanner> interaction_banner;
    std::optional<std::array<Rect,2>> resources;
};
HudFeatures inspect_hud(const Frame& frame,std::uint64_t generation);
bool game_hud_visible(const HudFeatures& hud);
class HudInspector {
    std::optional<Rect> menu_region_;
    std::vector<MenuControl> menu_hints_;
    Window window_;
    int width_{},height_{};
    std::uint64_t generation_{};
public:
    HudFeatures inspect(const Frame& frame,std::uint64_t generation);
};
Reading<std::uint64_t> read_honey(const Frame& frame,const std::vector<TextLine>& lines,std::uint64_t generation);
std::optional<std::array<Rect,2>> resource_hud_regions(const Frame& frame);
std::optional<std::array<Rect,2>> resource_hud_regions(const DetectionInput& input);
State read_game_text(const Frame& frame,const std::vector<TextLine>& lines,std::uint64_t generation);
}
