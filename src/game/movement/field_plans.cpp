#include "hive/game/movement/field_plans.hpp"
#include <algorithm>
#include <map>
#include <tuple>

namespace hive::game {
void validate_field_targets(const std::string& field,std::span<const std::string> alternatives) {
    auto valid=[](const std::string& value) {
        return !value.empty() && value.size()<=80 && std::none_of(value.begin(),value.end(),[](unsigned char c) { return c<32 || c==127; });
    };
    if (!valid(field) || alternatives.size()>63) throw std::invalid_argument("Invalid field request targets");
    std::set<std::string> names{field};
    for (const auto& name : alternatives) if (!valid(name) || !names.insert(name).second) throw std::invalid_argument("Invalid or duplicate field alternative");
}
bool accepts(const FieldGoal& goal,std::string_view field) {
    return goal.field==field || std::find(goal.alternatives.begin(),goal.alternatives.end(),field)!=goal.alternatives.end();
}
FieldDecision choose_field(const RouteNetwork& network,const std::vector<FieldPreference>& preferences,
    const std::vector<FieldGoal>& goals,const FieldContext& context,const FieldPolicy& policy) {
    const auto& observation=context.observation;
    if (!observation.generation || observation.now<Time::zero() || preferences.size()>64 || goals.size()>256)
        throw std::invalid_argument("Invalid field selection context");
    const bool observed=context.location.has_value(observation,Time(2000));
    const bool tracked=context.route_position && context.route_position->usable(observation);
    const bool use_observation=observed && (!tracked || context.location.captured>=context.route_position->context.now);
    const bool located=observed || tracked;
    const auto location=use_observation?context.location.value:tracked?context.route_position->location:std::string{};
    if (located && !network.contains(location)) throw std::invalid_argument("Unknown planning location");
    if (context.protection && (context.protection->field.empty() || context.protection->field.size()>80
        || !context.protection->generation || context.protection->until<Time::zero())) throw std::invalid_argument("Invalid field protection");
    const auto paths=located?network.paths_from(location,context.available,context.blocked_routes):std::map<std::string,Journey>{};
    struct Candidate {
        std::string field;
        std::optional<Journey> journey;
        std::vector<FieldGoal> goals;
        bool enabled{};
        std::size_t order{};
    };
    std::map<std::string,Candidate> candidates;
    for (const auto& preference : preferences) {
        if (preference.field.empty() || preference.field.size()>80 || !network.contains(preference.location)
            || candidates.contains(preference.field)) throw std::invalid_argument("Invalid field preference");
        Candidate candidate; candidate.field=preference.field; candidate.order=candidates.size(); candidate.enabled=preference.enabled;
        if (preference.enabled) if (const auto path=paths.find(preference.location);path!=paths.end()) candidate.journey=path->second;
        candidates.emplace(preference.field,std::move(candidate));
    }
    std::set<std::pair<std::string,std::string>> identities;
    std::vector<FieldGoal> active;
    for (const auto& goal : goals) {
        validate_field_targets(goal.field,goal.alternatives);
        if (goal.owner.empty() || goal.id.empty() || goal.owner.size()>80 || goal.id.size()>80 || goal.field.empty()
            || goal.field.size()>80 || goal.priority<0 || goal.priority>100 || goal.expires<Time::zero() || !goal.generation
            || !identities.emplace(goal.owner,goal.id).second) throw std::invalid_argument("Invalid or duplicate field goal");
        if (goal.complete || goal.generation!=observation.generation || goal.expires<=observation.now) continue;
        active.push_back(goal);
        for (auto& [field,candidate] : candidates) if (accepts(goal,field)) candidate.goals.push_back(goal);
    }
    auto stable=[](const FieldGoal& a,const FieldGoal& b) { return std::tie(a.owner,a.id)<std::tie(b.owner,b.id); };
    std::sort(active.begin(),active.end(),stable);
    FieldDecision decision;
    if (!located) { decision.reason=FieldReason::unknown_location; decision.deferred=std::move(active); return decision; }
    const Candidate* selected=nullptr;
    const bool protected_now=context.protection && context.protection->generation==observation.generation && context.protection->until>observation.now;
    if (protected_now) {
        auto found=candidates.find(context.protection->field);
        if (found!=candidates.end() && found->second.enabled && found->second.journey) selected=&found->second;
        decision.reason=selected?FieldReason::protected_field:FieldReason::protected_unavailable;
    } else {
        std::vector<FieldOption> options;
        for (const auto& [_,candidate] : candidates)
            if (candidate.enabled && candidate.journey) options.push_back({candidate.field,*candidate.journey,candidate.goals,candidate.order});
        if (const auto choice=policy.choose(options,context.current_field)) {
            const auto found=candidates.find(*choice);
            if (found==candidates.end() || !found->second.enabled || !found->second.journey)
                throw std::invalid_argument("Field policy selected an ineligible field");
            selected=&found->second;
            decision.reason=selected->goals.empty()?FieldReason::fallback:FieldReason::goal;
        }
    }
    if (selected) {
        decision.field=selected->field; decision.journey=selected->journey;
        decision.contributors=selected->goals;
        std::sort(decision.contributors.begin(),decision.contributors.end(),stable);
    }
    for (const auto& goal : active) if (!selected || !accepts(goal,selected->field)) decision.deferred.push_back(goal);
    return decision;
}
}
