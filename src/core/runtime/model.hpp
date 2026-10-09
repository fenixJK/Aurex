#pragma once
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <stdexcept>
#include <vector>

namespace hive {
using Time = std::chrono::milliseconds;
inline Time monotonic_now() { return std::chrono::duration_cast<Time>(std::chrono::steady_clock::now().time_since_epoch()); }
struct Point { double x{}, y{}; };
struct Rect {
    double x{}, y{}, width{}, height{};
    bool operator==(const Rect&) const = default;
    bool valid() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0; }
};
struct Window {
    std::uint64_t id{};
    std::int64_t process{};
    std::string title;
    Rect bounds;
};
struct WindowPoint {
    Window window;
    Point point;
    bool valid() const {
        return window.id && window.bounds.valid() && std::isfinite(point.x) && std::isfinite(point.y)
            && point.x>=0 && point.y>=0 && point.x<1 && point.y<1;
    }
    bool belongs_to(const Window& current) const {
        return valid() && window.id==current.id && window.process==current.process && window.bounds==current.bounds;
    }
};
struct Snapshot {
    Window window;
    bool focused{}, input_allowed{}, escape_down{}, pause_down{};
};
enum class Knowledge { unknown, absent, present };
struct ObservationContext { Window window; std::uint64_t generation{}; Time now{}; };
template<class T> struct Observation {
    Knowledge knowledge{Knowledge::unknown};
    T value{};
    Time captured{};
    Window window;
    std::uint64_t generation{};
    bool usable(const ObservationContext& current,Time max_age,Time minimum=Time{}) const {
        return knowledge!=Knowledge::unknown && current.window.id && current.window.bounds.valid()
            && captured>=minimum && captured<=current.now && current.now-captured<=max_age
            && generation==current.generation && window.id==current.window.id && window.process==current.window.process && window.bounds==current.window.bounds;
    }
    bool has_value(const ObservationContext& current,Time max_age,Time minimum=Time{}) const {
        return knowledge==Knowledge::present && usable(current,max_age,minimum);
    }
};
struct Frame {
    Window window;
    Time captured{};
    int width{}, height{};
    std::vector<std::uint8_t> rgba;
    bool valid() const { return width > 0 && height > 0 && window.bounds.valid() && rgba.size() == static_cast<std::size_t>(width)*height*4; }
    Point screen_point(Point pixel) const {
        if (!valid() || pixel.x < 0 || pixel.y < 0 || pixel.x >= width || pixel.y >= height)
            throw std::out_of_range("Point is outside the captured frame");
        return {window.bounds.x+pixel.x*window.bounds.width/width, window.bounds.y+pixel.y*window.bounds.height/height};
    }
};
struct TextLine { std::string text; Rect pixels; std::optional<float> confidence; };
enum class Key { forward, backward, left, right, jump, interact, rotate_left, rotate_right, primary, hotbar_1, hotbar_2, hotbar_3, hotbar_4, hotbar_5, hotbar_6, hotbar_7, pitch_up, pitch_down, zoom_in, zoom_out, shift_lock, digit_0, digit_8, digit_9, select_all, game_menu, respawn, confirm };
inline bool menu_key(Key key) { return key>=Key::game_menu && key<=Key::confirm; }
inline bool hotbar_key(Key key) { return key>=Key::hotbar_1 && key<=Key::hotbar_7; }
inline bool camera_key(Key key) {
    return key==Key::rotate_left || key==Key::rotate_right || key==Key::pitch_up
        || key==Key::pitch_down || key==Key::zoom_in || key==Key::zoom_out;
}
class PointerTasks;
class ActionPacing {
public:
    virtual ~ActionPacing() = default;
    virtual Time next(Time from,Time to) = 0;
};
struct Action {
    std::string label;
    std::vector<Key> held;
    Time duration{};
    std::optional<Point> pointer;
    int scroll{};
    std::optional<Point> pointer_to;
    bool continue_movement{};
    std::shared_ptr<ActionPacing> pacing;
    std::optional<Key> tap;
    std::shared_ptr<PointerTasks> concurrent_pointer;
    unsigned tap_count{1};
    Time tap_duration{};
};
inline bool allows_concurrent_pointer(const Action& action) {
    if (action.pointer || action.pointer_to || action.scroll || action.tap) return false;
    for (const auto key:action.held)
        if (!(key>=Key::forward && key<=Key::jump) && !camera_key(key)) return false;
    return true;
}
struct Job {
    std::string id;
    int priority{};
    Time due{};
    std::optional<Time> expires;
    std::vector<Action> actions;
    std::uint64_t owner{};
};
class Input {
public:
    virtual ~Input() = default;
    virtual void key(Key key, bool down) = 0;
    virtual void pointer(Point point) = 0;
    virtual void scroll(int) { throw std::runtime_error("Scrolling is not supported by this input adapter"); }
};
}
