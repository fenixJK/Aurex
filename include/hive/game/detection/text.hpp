#pragma once
#include "state.hpp"

namespace hive::game {
struct CountedSuffix {
    std::string singular,plural;
    unsigned maximum{};
};
struct TextTarget {
    std::string words;
    Rect region{0.2,0,0.6,0.3};
    bool cooldown{};
    bool station_name{};
    std::optional<CountedSuffix> counted_suffix;
};
void validate(const TextTarget& target);
Reading<bool> read_text_target(const Context& context,const State& state,const TextTarget& target,Time minimum={});
Reading<Time> read_text_cooldown(const Context& context,const State& state,const TextTarget& target,Time minimum={});
bool text_matches(const Frame& frame,const std::vector<TextLine>& lines,const TextTarget& target);
bool text_matches(const Context& context,const State& state,const TextTarget& target,Time minimum={});
}
