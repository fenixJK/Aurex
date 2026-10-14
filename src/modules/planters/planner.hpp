#pragma once
#include "core/runtime/model.hpp"
#include <array>
#include <map>
#include <set>
#include <span>

namespace hive::features::planters {
enum class Nectar { comforting, refreshing, satisfying, motivating, invigorating };
enum class Phase { build, maintain, recover };
struct Bounds { double lower{}, upper{}; };
struct Event { double seconds{}, amount{}; };
struct Target { double minimum{70}, buffer{10}; };
struct Stats {
    std::string planter, field;
    Nectar nectar{};
    double nectar_bonus{}, speed_bonus{}, full_seconds{};
    double rate() const { return nectar_bonus*speed_bonus/864.0; }
};
struct Travel { double seconds{90}; bool learned{}; };
struct Candidate { Stats stats; Travel travel; Time last_used{}; };
struct Group {
    Nectar nectar{};
    double current{};
    Target target;
    Phase phase{Phase::build};
    int priority{};
    std::string last_field, sipping_field;
    std::vector<Event> pending;
    std::vector<Candidate> candidates;
    bool current_known{true};
};
struct Assignment {
    Stats stats;
    Phase phase{};
    double growth{}, ready_in{}, useful{};
    std::string material;
};
struct Active { std::string planter, field, material; };
struct Selection { bool concentrated{}; std::optional<Nectar> omitted; };
Bounds bounds(Target target);
Phase phase(Phase previous,double current,Target target);
double forecast(double current,std::span<const Event> events,double seconds);
double floor_time(double current,std::span<const Event> events,double floor);
double interval(double current,std::span<const Event> events,double rate,double full_seconds,Target target,bool building,double delivery_delay=0);
void validate_configuration(std::span<const Group> groups,std::span<const Active> active,std::size_t capacity);
std::vector<Assignment> assign(std::span<const Group> groups,std::span<const Active> active,std::size_t capacity=3,Selection selection={},bool at_destination=false);
std::span<const Stats> catalog();
class TravelTimes {
    std::map<std::string,std::vector<double>> samples_;
public:
    void record(const std::string& field,double seconds);
    Travel estimate(const std::string& field) const;
};
enum class Operation { place, harvest };
enum class Outcome { pending, confirmed, uncertain };
struct Placements { std::set<std::string> identities; bool complete{}; std::string field; };
struct HarvestEvidence { std::string identity; Time latest{}; };
class Transaction {
    Operation operation_;
    ObservationContext started_;
    std::string identity_;
    Outcome outcome_{Outcome::pending};
    Time last_frame_{Time(-1)};
    Time minimum_{},deadline_{};
    bool input_finished_{};
public:
    Transaction(Operation operation,std::string identity,const ObservationContext& context,const Observation<Placements>& placements,bool attempt=false);
    void input_finished(const ObservationContext& context);
    bool input_was_finished() const { return input_finished_; }
    Outcome observe(const ObservationContext& context,const Observation<Placements>& placements,const Observation<HarvestEvidence>& harvested={});
    Outcome outcome() const { return outcome_; }
};
}
