#pragma once
#include "navigation.hpp"

namespace hive::automation {
struct NavigationView {
    workflows::NavigationObservations observations;
    std::set<std::string> available,blocked;
};
class NavigationPerception {
public:
    virtual ~NavigationPerception()=default;
    virtual void input_submitted(const std::vector<Action>&) const {}
    virtual NavigationView read(const ObservationContext& context,const game::State& state) const=0;
};
struct NavigationServices {
    std::shared_ptr<const workflows::RouteCatalog> routes;
    std::shared_ptr<const NavigationPerception> perception;
};
}
