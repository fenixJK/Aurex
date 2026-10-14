#pragma once
#include "materials.hpp"
#include <memory>

namespace hive::features::planters {
using WallTime = std::chrono::sys_time<Time>;
struct GrowthSample {
    WallTime observed;
    double fraction{};
    Time offline{};
    bool operator==(const GrowthSample&) const=default;
};
struct Plant {
    std::uint64_t id{};
    Stats stats;
    Phase phase{};
    WallTime placed, due;
    std::optional<GrowthSample> growth;
    Time offline{};
    std::string material;
};
double harvest_fraction(const Plant& plant);
double harvest_minimum_fraction(const Plant& plant);
double estimated_age(const Plant& plant,WallTime now);
WallTime ready_at(const Plant& plant);
struct Growth { std::string identity; double fraction{}; };
struct Intent {
    std::uint64_t ticket{};
    Operation operation{};
    Plant plant;
};
struct Ledger {
    std::uint64_t next_ticket{1};
    std::vector<Plant> plants;
    std::optional<Intent> pending;
    std::map<std::string,std::vector<double>> travel;
    std::map<std::string,WallTime> fields;
    MaterialHistory materials;
};
void validate(const Ledger& ledger);
bool exclude_offline(Ledger& ledger,WallTime begin,WallTime end);
std::string identity(const Stats& stats);
class Store {
public:
    virtual ~Store()=default;
    virtual std::optional<Ledger> load() const=0;
    virtual void save(const Ledger& ledger)=0;
};
struct ReturnedPlanter { std::string identity; Time first_seen{}; };
class Controller {
public:
    explicit Controller(std::shared_ptr<Store> store,std::size_t capacity=3);
    Controller(const Controller&)=delete;
    Controller& operator=(const Controller&)=delete;
    const Ledger& ledger() const { return ledger_; }
    bool needs_reconciliation() const { return ledger_.pending && !transaction_; }
    std::uint64_t place(const Assignment& assignment,const ObservationContext& context,WallTime wall,const Observation<Placements>& placements,bool attempt=false);
    std::uint64_t harvest(std::uint64_t plant,const ObservationContext& context,WallTime wall,const Observation<Placements>& placements,const Observation<Growth>& growth);
    WallTime refresh_growth(std::uint64_t plant,const ObservationContext& context,WallTime wall,const Observation<Placements>& placements,const Observation<Growth>& growth);
    Outcome observe(std::uint64_t ticket,const ObservationContext& context,WallTime wall,const Observation<Placements>& placements,
        const Observation<HarvestEvidence>& harvested={});
    void input_finished(std::uint64_t ticket,const ObservationContext& context);
    void placement_accepted(std::uint64_t ticket,const ObservationContext& context,WallTime wall);
    void interrupt() noexcept;
    void record_travel(const std::string& field,double seconds);
    void exclude_offline(WallTime begin,WallTime end);
    void abandon_unstarted_placement(std::uint64_t ticket,WallTime wall);
    bool reconcile_returned(const ObservationContext& context,WallTime wall,const Observation<ReturnedPlanter>& returned);
    bool reconcile(const ObservationContext& context,WallTime wall,const Observation<Placements>& placements);
private:
    std::shared_ptr<Store> store_;
    std::size_t capacity_;
    Ledger ledger_;
    std::optional<Transaction> transaction_;
    std::optional<WallTime> confirmed_at_;
    std::optional<ObservationContext> reconciliation_context_;
    Time reconciliation_frame_{Time(-1)};
    std::optional<WallTime> wall_;
    void clock(WallTime wall);
    void commit(Ledger next);
    void finish(bool present,WallTime wall);
    void reset_reconciliation();
    void baseline(const ObservationContext& context,const Observation<Placements>& placements,const std::string& present={}) const;
};
}
