#pragma once
#include "run.hpp"
#include "game/data/hive_preferences.hpp"
#include "core/module_host/module.hpp"

namespace hive::features::gathering {
struct PatternPreset {
    std::string name;
    std::function<std::vector<automation::Walk>(int)> generate;
    std::function<automation::ProgramFactory(int)> strategy;
    std::function<automation::ProgramFactory(const Settings&)> configured;
};
struct PatternControls {
    std::vector<PatternPreset> presets;
    std::string initial;
    int width{3},minutes{10},backpack{95};
};
struct SprinklerControls {
    std::function<std::vector<automation::Step>(const FieldPlan&,const std::string&,unsigned)> prepare;
    automation::ProgramFactory recenter;
    std::optional<game::SprinklerPreference> equipment;
};
Settings migrate_settings(int version,const Settings& saved);
extensions::Manifest module_manifest(const Plan& defaults,std::string id="hive.gathering",std::string provider="native-gathering",
    const std::optional<PatternControls>& controls={},const std::optional<SprinklerControls>& sprinklers={});
extensions::Factory module_factory(Plan defaults,automation::NavigationServices navigation,
    std::shared_ptr<const game::FieldPolicy> policy,std::shared_ptr<const game::FieldGoalSource> goals,
    std::optional<PatternControls> controls={},std::optional<SprinklerControls> sprinklers={});
}
