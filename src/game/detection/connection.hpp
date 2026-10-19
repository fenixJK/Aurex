#pragma once
#include "state.hpp"
namespace hive::game {
struct ConnectionView { Reading<Connection> connection; Reading<Point> reconnect; Reading<bool> home; };
std::vector<Rect> connection_dialog_regions(const Frame& frame);
ConnectionView read_connection_view(const Frame& frame,std::uint64_t generation,bool game_visible);
}
