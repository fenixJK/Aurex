#pragma once
#include "execution.hpp"
#include "core/module_host/module.hpp"

namespace hive::features::planters {
Settings migrate_settings(int version,const Settings& previous);
extensions::Manifest module_manifest(const ExecutionPlan& plan,std::string id="hive.planters",std::string provider="native-planters");
extensions::Factory module_factory(ExecutionPlan defaults,automation::NavigationServices navigation,std::shared_ptr<Account> account,
    std::shared_ptr<const Source> source,std::shared_ptr<const Actions> actions,WallClock clock,
    std::function<ObservationContext()> context={});
}
