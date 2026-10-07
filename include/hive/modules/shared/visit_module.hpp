#pragma once
#include "hive/automation/visits.hpp"
#include "hive/automation/navigation.hpp"
#include "visit_cooldowns.hpp"
#include "hive/module_host/module.hpp"
#include <string_view>

namespace hive::features::visits {
automation::ProgramFactory station_request(automation::VisitPlan plan,const std::string& id,
    std::shared_ptr<const game::RouteNetwork> network,std::shared_ptr<const automation::AvailabilitySource> availability,
    std::shared_ptr<Cooldowns> saved={});
using Demand=std::function<std::set<std::string>(const ObservationContext&)>;
std::string selection_key(std::string_view identity);
Settings migrate_selections(const Settings& values,const std::vector<std::string>& previous);
extensions::Manifest manifest(const automation::VisitPlan& plan,std::string id,std::string provider,std::string name,automation::Permissions permissions);
extensions::Factory factory(automation::VisitPlan defaults,automation::NavigationServices navigation,
    std::shared_ptr<const automation::AvailabilitySource> availability,automation::Permissions permissions,Demand demand={},std::shared_ptr<Cooldowns> saved={});
}
