#pragma once
#include "state.hpp"

namespace hive::game {
std::vector<Rect> connection_text_regions(const Frame& frame,bool in_game);
Reading<Connection> read_connection(const Frame& frame,const std::vector<TextLine>& lines,std::uint64_t generation);
bool client_home_screen(const TextSnapshot& text);
Observation<Point> read_reconnect(const Context& context,const Reading<TextSnapshot>& text);
}
