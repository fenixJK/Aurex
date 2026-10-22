#include "camera.hpp"

namespace hive::game {
CameraMove pitch_limit(Key direction) {
    if (direction!=Key::pitch_up && direction!=Key::pitch_down)
        throw std::invalid_argument("Camera pitch limit needs an up or down direction");
    // 15-degree taps must cover the entire -80 to +80 degree range.
    return {direction,11};
}
std::optional<CameraMove> heading_move(unsigned from,unsigned to) {
    if (from>7 || to>7) throw std::invalid_argument("Camera headings must be between zero and seven");
    const unsigned right=(to+8-from)%8;
    if (!right) return {};
    return CameraMove{right<=4?Key::rotate_right:Key::rotate_left,right<=4?right:8-right};
}
unsigned turned_heading(unsigned from,const CameraMove& move) {
    if (from>7 || !camera_key(move.key) || move.steps<1 || move.steps>32)
        throw std::invalid_argument("Invalid tracked camera turn");
    const unsigned steps=static_cast<unsigned>(move.steps%8);
    if (move.key==Key::rotate_right) return (from+steps)%8;
    if (move.key==Key::rotate_left) return (from+8-steps)%8;
    return from;
}
std::vector<Action> camera_actions(const CameraMove& move) {
    if (!camera_key(move.key) || move.steps<1 || move.steps>32 || move.press<Time(20)
        || move.press>Time(200) || move.release<Time(20) || move.release>Time(250))
        throw std::invalid_argument("Invalid camera movement");
    std::vector<Action> actions;
    actions.reserve(move.steps*2);
    for (std::size_t i=0;i<move.steps;++i) {
        actions.push_back({"Adjusting camera",{move.key},move.press,{}});
        actions.push_back({"Releasing camera key",{},move.release,{}});
    }
    return actions;
}
}
