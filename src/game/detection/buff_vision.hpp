#pragma once
#include "buff_catalog.hpp"
#include "buff_bar.hpp"
namespace hive::game::detail {
struct BuffMatch {
    std::size_t definition{};
    Rect cell;
    double error{};
    unsigned specificity{};
};
struct BuffScan {
    bool complete{};
    std::vector<BuffMatch> matches;
    std::vector<bool> ambiguous;
};
BuffScan scan_buffs(const DetectionInput&,const BuffBar&,std::span<const BuffDefinition>);
BuffState read_buff_value(const DetectionInput&,const BuffBar&,const BuffDefinition&,Rect cell);
BuffState read_buff_candidates(const DetectionInput&,const BuffBar&,const BuffDefinition&,
    std::span<const BuffMatch>,std::size_t definition_index);
}
