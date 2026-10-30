#include "camera.hpp"

namespace hive::game {
Time camera_settle(CameraFinish finish,Key key) {
    switch (finish) {
    case CameraFinish::input_only: return Time{};
    case CameraFinish::movement:
        if (key==Key::zoom_in || key==Key::zoom_out) return Time{};
        return key==Key::pitch_up || key==Key::pitch_down?Time(45):Time(35);
    case CameraFinish::observation: return Time(150);
    case CameraFinish::geometry: return Time(300);
    }
    throw std::invalid_argument("Invalid camera completion policy");
}
CameraMove pitch_limit(Key direction) {
    if (direction!=Key::pitch_up && direction!=Key::pitch_down)
        throw std::invalid_argument("Camera pitch limit needs an up or down direction");
    // 15-degree taps must cover the entire -80 to +80 degree range.
    return {direction,11};
}
std::optional<CameraMove> heading_move(unsigned from,unsigned to,CameraMove options) {
    if (from>7 || to>7) throw std::invalid_argument("Camera headings must be between zero and seven");
    const unsigned right=(to+8-from)%8;
    if (!right) return {};
    options.key=right<=4?Key::rotate_right:Key::rotate_left;
    options.steps=right<=4?right:8-right;
    return options;
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
    if (!camera_key(move.key) || move.steps<1 || move.steps>32
        || (move.input!=CameraInput::automatic && move.input!=CameraInput::held && move.input!=CameraInput::tapped)
        || (move.input==CameraInput::tapped && move.hold))
        throw std::invalid_argument("Invalid camera movement");
    const auto hold=move.hold.value_or(CameraHold{});
    if (hold.press<Time(20) || hold.press>Time(200) || hold.release<Time(20) || hold.release>Time(250))
        throw std::invalid_argument("Invalid camera key timing");
    const auto settle=camera_settle(move.finish,move.key);
    // Rotations are discrete taps. Zoom keeps spaced presses so Roblox can
    // apply each distance change. Authored routes can explicitly retain holds.
    const bool held=move.input==CameraInput::held || (move.input==CameraInput::automatic
        && (move.hold || move.key==Key::zoom_in || move.key==Key::zoom_out));
    std::vector<Action> actions;
    if (!held) {
        for (std::size_t remaining=move.steps;remaining;) {
            Action action{"Adjusting camera",{},Time(1),{}};
            action.tap=move.key;
            action.tap_count=static_cast<unsigned>(std::min(remaining,std::size_t(8)));
            remaining-=action.tap_count;
            actions.push_back(std::move(action));
        }
        // Settle once after the entire turn, measured after input delivery.
        actions.back().duration=std::max(Time(1),settle);
        return actions;
    }
    actions.reserve(move.steps*2);
    for (std::size_t i=0;i<move.steps;++i) {
        actions.push_back({"Adjusting camera",{move.key},hold.press,{}});
        actions.push_back({"Releasing camera key",{},hold.release,{}});
    }
    actions.back().duration+=settle;
    return actions;
}
Time camera_duration(const CameraMove& move) {
    Time result{};
    for (const auto& action:camera_actions(move)) result+=action.duration;
    return result;
}
}
