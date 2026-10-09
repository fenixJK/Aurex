#pragma once
#include "detection.hpp"

namespace hive::game::detail {
std::optional<unsigned> read_buff_stack(const DetectionInput&,Rect cell,double scale,unsigned maximum);
}
