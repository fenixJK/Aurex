#pragma once
#include "plan.hpp"
#include "../shared/visit_module.hpp"

namespace hive::features::collection {
Settings migrate_item_settings(Settings values);
extensions::Manifest module_manifest(const Plan& plan,std::string id="hive.collection",std::string provider="native-collection",bool requests=false);
extensions::Factory module_factory(Plan plan,automation::NavigationServices navigation,std::shared_ptr<const automation::AvailabilitySource> availability,std::shared_ptr<visits::Cooldowns> saved={},std::shared_ptr<const game::RequestSource> requests={});
}
