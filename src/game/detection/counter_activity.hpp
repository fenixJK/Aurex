#pragma once
#include "core/runtime/model.hpp"

namespace hive::game {
// Tracks changes to the bright digits in an already located HUD counter.
// This is activity evidence, not OCR or a numerical reading.
class CounterActivity {
    std::vector<std::uint8_t> previous_;
    Time changed_{},captured_{};
public:
    std::optional<Time> observe(const Frame& frame,Rect pixels);
};
}
