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
    bool operator==(const FixedLabelQuery&) const=default;
};
// Fixed UI words only. Variable values still belong to the requested OCR region.
std::vector<TextLine> fixed_ui_labels(const Frame& frame,Rect region,std::span<const std::string_view> labels,LabelInk ink=LabelInk::light);
Reading<TextSnapshot> with_fixed_labels(const ObservationContext&,const Reading<TextSnapshot>& text,
    const Reading<TextSnapshot>& labels,std::span<const std::string_view> names);
}
