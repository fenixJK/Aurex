#pragma once
#include "plan.hpp"
#include "modules/shared/visit_module.hpp"
#include "vichop/hop.hpp"

namespace hive::features::combat {
Settings migrate_settings(int version,const Settings& values,const Plan& plan);
extensions::Manifest module_manifest(const Plan& plan,std::string id="hive.combat",std::string provider="native-combat",bool requests=false);
extensions::Factory module_factory(Plan plan,automation::NavigationServices navigation,std::shared_ptr<const automation::AvailabilitySource> availability,std::shared_ptr<const game::EnemyGoalSource> goals={},std::shared_ptr<visits::Cooldowns> saved={},std::optional<Plan> configuration={},std::optional<vichop::Services> hopping={});
}
