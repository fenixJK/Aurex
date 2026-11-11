#pragma once
#include "buff_icon.hpp"
#include "buffs.hpp"
#include <span>

namespace hive::game::detail {
// Coordinates are relative to a nominal 38 px HUD cell, never a screen position.
struct BuffColorCheck {
    Rect region;
    std::array<int,3> color{};
    std::array<double,3> tolerance{30,30,30};
    BuffColorCheck(Rect area,std::array<int,3> rgb,double error)
        :region(area),color(rgb),tolerance{error,error,error} {}
    BuffColorCheck(Rect area,std::array<int,3> rgb,std::array<double,3> error)
        :region(area),color(rgb),tolerance(error) {}
};
enum class BuffNormalization { whole, rows, brightness };
struct BuffAppearance {
    const BuffIcon* icon{};
    Rect region{3,2,32,21};
    int first_offset{-2},last_offset{2};
    double color_limit{45};
    std::vector<BuffColorCheck> color_checks;
    BuffNormalization normalization{BuffNormalization::whole};
    // A full-body reference with identifying color anchors outranks a shared silhouette.
    unsigned specificity{};
    double mean_color_limit{255};
    double minimum_margin{5};
    double pixel_error_limit{255},upscaled_pixel_slack{};
    double minimum_edge_similarity{.8};
    double maximum_noise_ratio{.4};
};
enum class BuffValueKind { presence, stacks, meter };
struct BuffMeter {
    std::array<int,3> color{};
    double minimum{},range{1};
};
struct BuffMovementEffect {
    double multiplier{1},per_stack{},flat{};
    unsigned maximum_stacks{};
    bool unknown_stacks_allowed{};
};
struct BuffDefinition {
    BuffType type;
    std::vector<BuffAppearance> appearances;
    BuffValueKind value_kind{BuffValueKind::presence};
    std::optional<BuffAppearance> single_stack;
    std::optional<BuffMeter> meter;
    bool missing_is_unknown{};
    bool uncaptioned_single{true};
    std::vector<BuffMovementEffect> movement;
};
std::span<const BuffDefinition> report_buff_definitions();
std::span<const BuffDefinition> movement_buff_definitions();
}
