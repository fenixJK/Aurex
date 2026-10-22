#pragma once
#include "core/runtime/model.hpp"

namespace hive::game {
struct CameraMove {
    Key key{Key::rotate_right};
    std::size_t steps{1};
    Time press{50},release{50};
};
std::vector<Action> camera_actions(const CameraMove& move);
CameraMove pitch_limit(Key direction);
std::optional<CameraMove> heading_move(unsigned from,unsigned to);
unsigned turned_heading(unsigned from,const CameraMove& move);
}
