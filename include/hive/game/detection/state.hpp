#pragma once
#include "hive/core/model.hpp"

namespace hive::game {
using Context = ObservationContext;
template<class T> using Reading = Observation<T>;
enum class HivePrompt { make_honey, stop_making_honey, none, collect_pollen };
enum class Connection { connected, disconnected };
struct TextSnapshot { int width{},height{}; std::vector<TextLine> lines; };
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
    Reading<bool> game_hud;
    Reading<bool> alive, at_hive;
    Reading<double> backpack_percent;
    Reading<HivePrompt> hive_prompt;
    Reading<TextSnapshot> text;
    Reading<double> movement_speed;
    Reading<bool> shift_lock;
    Reading<Point> open_menu;
    Reading<std::vector<MenuControl>> menus;
    Reading<DialogButtons> confirmation;
    Reading<std::uint64_t> honey;
};
Reading<HivePrompt> read_hive_prompt(const Frame& frame,const std::vector<TextLine>& lines,Rect region,std::uint64_t generation);
}
