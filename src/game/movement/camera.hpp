#pragma once
#include "core/runtime/model.hpp"

namespace hive::game {
enum class CameraInput { automatic, held, tapped };
enum class CameraFinish { input_only, movement, observation, geometry };
struct CameraHold {
    Time press{50},release{50};
};
struct CameraMove {
    Key key{Key::rotate_right};
    std::size_t steps{1};
    CameraInput input{CameraInput::automatic};
    CameraFinish finish{CameraFinish::movement};
    std::optional<CameraHold> hold;
};
Time camera_settle(CameraFinish finish,Key key=Key::rotate_right);
std::vector<Action> camera_actions(const CameraMove& move);
Time camera_duration(const CameraMove& move);
CameraMove pitch_limit(Key direction);
std::optional<CameraMove> heading_move(unsigned from,unsigned to,CameraMove options={});
unsigned turned_heading(unsigned from,const CameraMove& move);
}
