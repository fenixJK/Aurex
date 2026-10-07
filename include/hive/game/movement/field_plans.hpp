#pragma once
#include "navigation.hpp"
#include <span>
#include <string_view>

namespace hive::game {
struct FieldPreference {
    std::string field, location;
    bool enabled{true};
};
struct FieldGoal {
    std::string owner, id, field;
    int priority{};
    Time expires{};
    std::uint64_t generation{};
    std::vector<std::string> alternatives;
    bool complete{};
    unsigned hotbar_slot{};
    bool interrupt{};
};
void validate_field_targets(const std::string& field,std::span<const std::string> alternatives);
bool accepts(const FieldGoal& goal,std::string_view field);
struct FieldProtection {
    std::string field;
    Time until{};
    std::uint64_t generation{};
};
struct FieldContext {
    ObservationContext observation;
    Observation<std::string> location;
    std::optional<std::string> current_field;
    std::optional<FieldProtection> protection;
    std::set<std::string> available, blocked_routes;
    std::optional<RoutePosition> route_position;
};
enum class FieldReason { goal, protected_field, fallback, no_field, unknown_location, protected_unavailable };
struct FieldDecision {
    FieldReason reason{FieldReason::no_field};
    std::optional<std::string> field;
    std::optional<Journey> journey;
    std::vector<FieldGoal> contributors;
    std::vector<FieldGoal> deferred;
};
struct FieldOption {
    std::string field;
    Journey journey;
    std::vector<FieldGoal> goals;
    std::size_t saved_order{};
};
class FieldPolicy {
public:
    virtual ~FieldPolicy() = default;
    virtual std::optional<std::string> choose(std::span<const FieldOption> options,
        const std::optional<std::string>& current_field) const = 0;
};
FieldDecision choose_field(const RouteNetwork& network,const std::vector<FieldPreference>& preferences,
    const std::vector<FieldGoal>& goals,const FieldContext& context,const FieldPolicy& policy);
}
