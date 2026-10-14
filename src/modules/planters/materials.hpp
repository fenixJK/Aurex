#pragma once
#include "planner.hpp"

namespace hive::features::planters {
struct MaterialRoute {
    std::string id,item,planter;
    std::vector<std::string> fields;
    unsigned amount{};
    Time cooldown;
    std::string goal;
    bool once{};
};
struct MaterialProgress { unsigned step{}; Time ready{}; bool complete{}; bool operator==(const MaterialProgress&) const=default; };
using MaterialHistory=std::map<std::string,MaterialProgress>;
enum class Mode { nectar, items };
struct MaterialPlan {
    std::vector<std::string> goals;
    MaterialHistory history;
    Time now{};
    Mode mode{Mode::nectar};
    bool secondary{};
    std::set<std::string> excluded_routes;
    bool needs_nectar() const { return mode==Mode::nectar || secondary; }
    bool farms_items() const { return mode==Mode::items || secondary; }
};
std::span<const MaterialRoute> material_routes();
std::string material_goal(const std::string& intent);
bool valid_material(const std::string& intent,const Stats& stats);
void validate_materials(const MaterialHistory& history);
void material_harvest(MaterialHistory& history,const Stats& stats,bool full,Time now,const std::string& intent={});
std::vector<Assignment> material_choices(std::span<const Group> groups,std::span<const Active> occupied,
    std::size_t capacity,Selection selection,const MaterialPlan& plan);
std::vector<Assignment> placement_choices(std::span<const Group> groups,std::span<const Active> occupied,
    std::size_t capacity,Selection selection,const MaterialPlan& plan,bool at_destination=false);
std::optional<Time> next_material_ready(const MaterialPlan& plan,Time after);
}
