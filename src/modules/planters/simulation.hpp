#pragma once
#include "planning.hpp"
#include <functional>

namespace hive::features::planters {
enum class NectarReading { exact, hud };
struct SimulationConfig {
    std::vector<Group> groups;
    double seconds{7*86400};
    std::size_t capacity{3};
    std::vector<Growing> growing;
    Selection selection;
    std::function<Selection(const PlanningSnapshot&)> choose;
    bool record_segments{};
    std::size_t max_steps{100000};
    std::function<bool()> cancelled;
    NectarReading readings{NectarReading::hud};
    MaterialPlan materials;
};
struct Segment { double begin{},end{}; std::array<double,5> levels{}; };
struct SimulationResult {
    std::array<double,5> final{}, coverage_seconds{};
    std::array<std::optional<double>,5> built_at;
    std::size_t placements{}, harvests{};
    double travel_seconds{};
    std::vector<Segment> segments;
    std::vector<Growing> growing;
    std::map<std::string,unsigned> bonuses,base_items,item_harvests;
};
SimulationResult simulate(SimulationConfig config);
}
