#pragma once
#include "detection.hpp"

namespace hive::game::detail {
struct BuffStackEvidence {
    bool uncaptioned_single{true};
    std::optional<double> single_stack_similarity;
};
std::optional<unsigned> read_buff_stack(const DetectionInput&,Rect cell,double scale,unsigned maximum,BuffStackEvidence evidence={});
}
