#pragma once
#include "detection.hpp"
#include <array>

namespace hive::game {
using CardImage=std::array<std::uint8_t,16*16*3>;
struct MemoryCard { Rect bounds; CardImage image; };
struct MemoryBoard final : Detection { std::vector<MemoryCard> cards; Knowledge visibility{Knowledge::unknown}; };
double card_distance(const CardImage&,const CardImage&);
std::shared_ptr<const Detector> memory_match_detector();
}
