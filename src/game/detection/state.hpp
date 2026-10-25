#pragma once
#include "core/runtime/model.hpp"

namespace hive::game {
using Context = ObservationContext;
template<class T> using Reading = Observation<T>;
enum class HivePrompt { make_honey, stop_making_honey, none, collect_pollen };
enum class Connection { connected, disconnected };
struct TextSnapshot { int width{},height{}; std::vector<TextLine> lines; };
struct InteractionCaption { std::string label; bool cooldown{}; };
enum class InteractionBanner { none, ready, cooldown };
struct DialogButtons { Point yes,no; };
enum class MenuTab { inventory,quests,settings };
struct MenuControl {
    MenuTab tab;
    Rect bounds;
    std::optional<bool> selected;
    Point center() const { return {bounds.x+bounds.width/2,bounds.y+bounds.height/2}; }
};
struct State {
    Reading<Connection> connection;
    Reading<Point> reconnect;
    Reading<bool> client_home;
    Reading<bool> game_hud;
    Reading<bool> alive, at_hive;
    Reading<bool> interaction_prompt;
    Reading<double> backpack_percent;
    Reading<Time> pollen_changed_at;
    Reading<HivePrompt> hive_prompt;
    Reading<TextSnapshot> text;
    Reading<TextSnapshot> fixed_labels;
    Reading<InteractionCaption> interaction_caption;
    Reading<InteractionBanner> interaction_banner;
    Reading<double> movement_speed;
    Reading<bool> shift_lock;
    Reading<Point> open_menu;
    Reading<std::vector<MenuControl>> menus;
    Reading<DialogButtons> confirmation;
    Reading<bool> last_death;
};
Reading<HivePrompt> read_hive_prompt(const Frame& frame,const std::vector<TextLine>& lines,Rect region,std::uint64_t generation);
}
