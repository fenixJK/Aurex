#include "hive/game/movement/navigation.hpp"
#include <algorithm>
#include <map>

namespace hive::game {
namespace {
bool identifier(const std::string& value) {
    return !value.empty() && value.size()<=80
        && std::none_of(value.begin(),value.end(),[](unsigned char c) { return c<32 || c==127; });
}
bool before(const Journey& a,const Journey& b) {
    if (a.estimate!=b.estimate) return a.estimate<b.estimate;
    if (a.routes.size()!=b.routes.size()) return a.routes.size()<b.routes.size();
    return a.routes<b.routes;
}
}
RouteNetwork::RouteNetwork(std::vector<std::string> locations,std::vector<Route> routes):routes_(std::move(routes)) {
    if (locations.empty() || locations.size()>128 || routes_.size()>1024) throw std::invalid_argument("Invalid route catalog size");
    for (const auto& location : locations)
        if (!identifier(location) || !locations_.insert(location).second) throw std::invalid_argument("Invalid or duplicate location");
    std::set<std::string> ids;
    for (const auto& route : routes_) {
        if (!identifier(route.id) || !ids.insert(route.id).second || !contains(route.from) || !contains(route.to)
            || route.from==route.to || route.estimate<Time(1) || route.estimate>std::chrono::hours(24)
            || route.timeout<Time(100) || route.timeout>std::chrono::minutes(30)
            || route.requirements.size()+route.departure_requirements.size()>32
            || (route.arrival!=RouteArrival::observed && route.arrival!=RouteArrival::estimated)) throw std::invalid_argument("Invalid route");
        for (const auto& requirement : route.requirements)
            if (!identifier(requirement)) throw std::invalid_argument("Invalid route requirement");
        for (const auto& requirement : route.departure_requirements)
            if (!identifier(requirement)) throw std::invalid_argument("Invalid departure requirement");
    }
}
bool RouteNetwork::contains(const std::string& location) const { return locations_.contains(location); }
const Route& RouteNetwork::route(const std::string& id) const {
    const auto found=std::find_if(routes_.begin(),routes_.end(),[&](const Route& route) { return route.id==id; });
    if (found==routes_.end()) throw std::invalid_argument("Unknown route");
    return *found;
}
std::map<std::string,Journey> RouteNetwork::paths_from(const std::string& from,
    const std::set<std::string>& available,const std::set<std::string>& blocked) const {
    if (!contains(from)) throw std::invalid_argument("Unknown route location");
    if (available.size()>128 || blocked.size()>1024) throw std::invalid_argument("Route constraints exceed limits");
    for (const auto& id : blocked)
        if (std::none_of(routes_.begin(),routes_.end(),[&](const Route& route) { return route.id==id; }))
            throw std::invalid_argument("Unknown blocked route");
    std::map<std::string,Journey> best{{from,{}}};
    std::set<std::string> visited;
    while (true) {
        auto current=best.end();
        for (auto it=best.begin();it!=best.end();++it)
            if (!visited.contains(it->first) && (current==best.end() || before(it->second,current->second))) current=it;
        if (current==best.end()) return best;
        visited.insert(current->first);
        for (const auto& route : routes_) {
            if (route.from!=current->first || visited.contains(route.to) || blocked.contains(route.id)
                || !std::includes(available.begin(),available.end(),route.requirements.begin(),route.requirements.end())
                || !std::includes(available.begin(),available.end(),route.departure_requirements.begin(),route.departure_requirements.end())) continue;
            auto candidate=current->second;
            candidate.estimate+=route.estimate;
            candidate.routes.push_back(route.id);
            const auto found=best.find(route.to);
            if (found==best.end() || before(candidate,found->second)) best.insert_or_assign(route.to,std::move(candidate));
        }
    }
}
std::optional<Journey> RouteNetwork::plan(const std::string& from,const std::string& to,
    const std::set<std::string>& available,const std::set<std::string>& blocked) const {
    if (!contains(to)) throw std::invalid_argument("Unknown route location");
    auto paths=paths_from(from,available,blocked);
    if (const auto found=paths.find(to);found!=paths.end()) return std::move(found->second);
    return {};
}
}
