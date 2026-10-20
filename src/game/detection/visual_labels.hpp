#pragma once
#include "state.hpp"
#include <span>
#include <string_view>

namespace hive::game {
enum class LabelInk { light, dark, either };
struct FixedLabelQuery {
    Rect region;
    std::vector<std::string> labels;
    LabelInk ink{LabelInk::light};
    // Optional region relative to an earlier label's width, from the same frame.
    std::string anchor;
    bool rows{};
    bool operator==(const FixedLabelQuery&) const=default;
};
// Known UI words matched from compact masks, independently of changing numeric prefixes.
std::vector<TextLine> fixed_ui_labels(const Frame& frame,Rect region,std::span<const std::string_view> labels,LabelInk ink=LabelInk::light);
std::vector<TextLine> fixed_ui_labels(const Frame&,const FixedLabelQuery&,std::span<const TextLine> anchors={});
Reading<TextSnapshot> with_fixed_labels(const ObservationContext&,const Reading<TextSnapshot>& text,
    const Reading<TextSnapshot>& labels,std::span<const std::string_view> names);
}
