#pragma once
#include "game/detection/state.hpp"
#include "game/detection/visual_labels.hpp"
#include <map>
#include <set>

namespace hive::features::tools {
struct Stat { std::string name; double minimum{},maximum{}; bool multiplier{}; };
const std::vector<Stat>& amulet_stats();
const std::vector<std::string>& passives();
const std::vector<std::string>& bees();
struct Amulet { std::map<std::string,double> stats; std::set<std::string> passives; std::optional<Rect> popup; };
struct AmuletFilter {
    std::string main{"Pop Star"};
    std::set<std::string> sides;
    std::map<std::string,double> minimums;
    bool double_passive{};
};
struct Bee { std::string name; bool gifted{},mythic{}; };
void validate(const AmuletFilter& filter);
bool matches(const Amulet& result,const AmuletFilter& filter);
Observation<Amulet> read_amulet(const ObservationContext&,const game::State&);
std::vector<game::FixedLabelQuery> amulet_labels();
std::vector<Rect> amulet_number_regions(const ObservationContext&,const game::State&);
std::vector<game::FixedLabelQuery> bee_labels();
std::vector<game::FixedLabelQuery> jelly_labels();
Observation<Rect> read_jelly_button(const ObservationContext&,const game::State&);
struct JellyResult { Bee bee; std::optional<Point> again; };
Observation<JellyResult> read_jelly(const ObservationContext&,const game::State&);
}
