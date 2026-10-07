#pragma once
#include "hive/game/detection/state.hpp"
#include <map>
#include <set>

namespace hive::features::tools {
struct Stat { std::string name; double minimum{},maximum{}; bool multiplier{}; };
const std::vector<Stat>& amulet_stats();
const std::vector<std::string>& passives();
const std::vector<std::string>& bees();
struct Amulet { std::map<std::string,double> stats; std::set<std::string> passives; };
struct AmuletFilter {
    std::string main{"Pop Star"};
    std::set<std::string> sides;
    std::map<std::string,double> minimums;
    bool double_passive{};
};
struct Bee { std::string name; bool gifted{},mythic{}; };
struct BeeFilter {
    std::set<std::string> names;
    bool stop_gifted{},stop_mythic{};
};
void validate(const AmuletFilter& filter);
void validate(const BeeFilter& filter,bool allow_any=false);
bool matches(const Amulet& result,const AmuletFilter& filter);
bool matches(const Bee& result,const BeeFilter& filter);
Observation<Amulet> read_amulet(const ObservationContext&,const game::State&);
struct JellyResult { Bee bee; std::optional<Point> again; };
Observation<JellyResult> read_jelly(const ObservationContext&,const game::State&);
}
