#pragma once
#include "core/runtime/model.hpp"
#include <map>
#include <set>

namespace hive::game {
enum class PositionBasis { observed, route_execution };
struct RouteExecution {
    std::string route,from,to;
    std::size_t checkpoint{};
    Time entered{};
    ObservationContext context;
    std::string observation_scope;
    bool usable(const ObservationContext& current,Time age=Time(2000)) const {
        return !route.empty() && !from.empty() && !to.empty() && checkpoint<64 && entered>=Time{} && entered<=context.now
            && Observation<bool>{Knowledge::present,true,context.now,context.window,context.generation}.has_value(current,age);
    }
};
struct RoutePosition {
    std::string location;
    PositionBasis basis{PositionBasis::observed};
    ObservationContext context;
    bool usable(const ObservationContext& current,Time age=Time(2000)) const {
        return context.generation && !location.empty() && location.size()<=80 && age>=Time{}
            && (basis==PositionBasis::observed || basis==PositionBasis::route_execution)
            && Observation<bool>{Knowledge::present,true,context.now,context.window,context.generation}.has_value(current,age);
    }
};
enum class RouteArrival { observed, estimated };
struct Route {
    std::string id, from, to;
    Time estimate{};
    std::set<std::string> requirements;
    RouteArrival arrival{RouteArrival::observed};
    std::set<std::string> departure_requirements;
    Time timeout{120000};
};
struct Journey {
    std::vector<std::string> routes;
    Time estimate{};
};
class RouteNetwork {
    std::set<std::string> locations_;
    std::vector<Route> routes_;
public:
    RouteNetwork(std::vector<std::string> locations,std::vector<Route> routes);
    bool contains(const std::string& location) const;
    const std::set<std::string>& locations() const { return locations_; }
    const std::vector<Route>& routes() const { return routes_; }
    const Route& route(const std::string& id) const;
    std::map<std::string,Journey> paths_from(const std::string& from,
        const std::set<std::string>& available={},const std::set<std::string>& blocked={}) const;
    std::optional<Journey> plan(const std::string& from,const std::string& to,
        const std::set<std::string>& available={},const std::set<std::string>& blocked={}) const;
};
}
