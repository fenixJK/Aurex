#include "controller.hpp"
#include <algorithm>
#include <limits>

namespace hive::features::planters {
namespace {
bool name(const std::string& value) {
    return !value.empty() && value.size()<=80 && std::none_of(value.begin(),value.end(),[](unsigned char c) { return c<32 || c==127 || c=='/'; });
}
void plant_valid(const Plant& plant) {
    const auto& s=plant.stats;
    if (!plant.material.empty() && (plant.phase!=Phase::build || !valid_material(plant.material,s)))
        throw std::invalid_argument("Invalid material planter");
    if (!plant.id || !name(s.planter) || !name(s.field) || s.nectar<Nectar::comforting || s.nectar>Nectar::invigorating
        || plant.phase<Phase::build || plant.phase>Phase::recover || !std::isfinite(s.nectar_bonus) || s.nectar_bonus<=0 || s.nectar_bonus>10
        || !std::isfinite(s.speed_bonus) || s.speed_bonus<=0 || s.speed_bonus>10 || !std::isfinite(s.full_seconds)
        || s.full_seconds<1 || s.full_seconds>86400 || plant.placed.time_since_epoch()<Time::zero()
        || plant.due.time_since_epoch()>Time::max()-std::chrono::hours(24)
        || plant.offline<Time{} || plant.offline>Time::max()-plant.due.time_since_epoch()-std::chrono::hours(24)
        || plant.due<=plant.placed || plant.due-plant.placed<Time(1000) || plant.due-plant.placed>std::chrono::hours(24)
        || (plant.phase==Phase::build && (plant.due-plant.placed).count()<std::round(s.full_seconds)*1000)) throw std::invalid_argument("Invalid saved planter");
    if (plant.growth && (plant.growth->observed<plant.placed || plant.growth->observed.time_since_epoch()>Time::max()-std::chrono::hours(24)
        || plant.growth->offline<Time{} || plant.growth->offline>plant.offline
        || !std::isfinite(plant.growth->fraction) || plant.growth->fraction<0 || plant.growth->fraction>1))
        throw std::invalid_argument("Invalid saved planter growth");
}
bool same(const Plant& a,const Plant& b) {
    return a.id==b.id && a.stats.planter==b.stats.planter && a.stats.field==b.stats.field && a.stats.nectar==b.stats.nectar
        && a.stats.nectar_bonus==b.stats.nectar_bonus && a.stats.speed_bonus==b.stats.speed_bonus && a.stats.full_seconds==b.stats.full_seconds
        && a.phase==b.phase && a.placed==b.placed && a.due==b.due && a.growth==b.growth && a.offline==b.offline && a.material==b.material && a.missing==b.missing;
}
void scan_valid(const Placements& scan) {
    if ((!scan.field.empty() && !name(scan.field)) || scan.identities.size()>3) throw std::invalid_argument("Too many observed planters");
    std::set<std::string> types,fields;
    for (const auto& value : scan.identities) {
        const auto split=value.find('/');
        if (split==std::string::npos || !name(value.substr(0,split)) || !name(value.substr(split+1))
            || (!scan.field.empty() && value.substr(split+1)!=scan.field)
            || !types.insert(value.substr(0,split)).second || !fields.insert(value.substr(split+1)).second)
            throw std::invalid_argument("Conflicting planter observation");
    }
}
void wall_valid(WallTime wall) {
    if (wall.time_since_epoch()<Time::zero() || wall.time_since_epoch()>Time::max()-std::chrono::hours(24)) throw std::invalid_argument("Invalid planter wall clock");
}
}
double harvest_fraction(const Plant& plant) {
    plant_valid(plant);
    return plant.phase==Phase::build?1.0:std::min(1.0,(plant.due-plant.placed).count()/(plant.stats.full_seconds*1000));
}
double harvest_minimum_fraction(const Plant& plant) {
    const auto target=harvest_fraction(plant);
    return plant.material.empty()?target-std::min(.02,target*.05):1.0;
}
double estimated_age(const Plant& plant,WallTime now) {
    plant_valid(plant); wall_valid(now);
    const auto origin=plant.growth?plant.growth->observed:plant.placed;
    if (now<origin) throw std::invalid_argument("Saved planter growth is in the future");
    const auto offline=plant.offline-(plant.growth?plant.growth->offline:Time{});
    return (plant.growth?plant.growth->fraction*plant.stats.full_seconds:0)+std::max(Time{},now-origin-offline).count()/1000.0;
}
WallTime ready_at(const Plant& plant) {
    const auto target=harvest_fraction(plant);
    if (!plant.growth) return plant.due+plant.offline;
    if (plant.growth->fraction>=target) return plant.growth->observed;
    return plant.growth->observed+plant.offline-plant.growth->offline
        +Time(static_cast<Time::rep>(std::ceil(std::max(0.,target-plant.growth->fraction)*plant.stats.full_seconds*1000)));
}
std::string identity(const Stats& stats) {
    if (!name(stats.planter) || !name(stats.field)) throw std::invalid_argument("Invalid planter identity");
    return stats.planter+"/"+stats.field;
}
void validate(const Ledger& ledger) {
    validate_materials(ledger.materials);
    if (!ledger.next_ticket || ledger.plants.size()>3) throw std::invalid_argument("Invalid planter ledger");
    std::set<std::uint64_t> ids; std::set<std::string> types,fields;
    for (const auto& plant : ledger.plants) {
        plant_valid(plant);
        if (plant.id>=ledger.next_ticket || !ids.insert(plant.id).second || !types.insert(plant.stats.planter).second || !fields.insert(plant.stats.field).second)
            throw std::invalid_argument("Conflicting saved planters");
    }
    if (ledger.travel.size()>17) throw std::invalid_argument("Too many planter travel records");
    for (const auto& [field,samples]:ledger.travel) {
        if (!name(field) || samples.empty() || samples.size()>3) throw std::invalid_argument("Invalid planter travel history");
        for (double seconds:samples) if (!std::isfinite(seconds) || seconds<1 || seconds>1800) throw std::invalid_argument("Invalid planter travel sample");
    }
    if (ledger.fields.size()>17) throw std::invalid_argument("Too many planter field records");
    for (const auto& [field,used]:ledger.fields) { if (!name(field)) throw std::invalid_argument("Invalid planter field history"); wall_valid(used); }
    if (!ledger.pending) return;
    const auto& intent=*ledger.pending; plant_valid(intent.plant);
    if (!intent.ticket || intent.ticket>=ledger.next_ticket || ids.contains(intent.ticket)) throw std::invalid_argument("Invalid planter action ticket");
    if (intent.operation==Operation::place) {
        if (intent.plant.id!=intent.ticket || intent.plant.growth || ledger.plants.size()>=3 || types.contains(intent.plant.stats.planter) || fields.contains(intent.plant.stats.field))
            throw std::invalid_argument("Conflicting pending placement");
    } else if (intent.operation==Operation::harvest) {
        if (std::none_of(ledger.plants.begin(),ledger.plants.end(),[&](const Plant& p) { return same(p,intent.plant); }))
            throw std::invalid_argument("Pending harvest does not match a saved planter");
    } else throw std::invalid_argument("Invalid planter operation");
}
bool exclude_offline(Ledger& ledger,WallTime begin,WallTime end) {
    wall_valid(begin); wall_valid(end);
    if (end<begin) throw std::invalid_argument("Invalid planter offline interval");
    bool changed=false;
    const auto adjust=[&](Plant& plant) {
        const auto duration=std::max(Time{},end-std::max(begin,plant.placed));
        if (duration==Time{}) return;
        plant.offline+=duration; changed=true;
        if (plant.growth) plant.growth->offline+=std::max(Time{},std::min(end,plant.growth->observed)-std::max(begin,plant.placed));
    };
    for (auto& plant:ledger.plants) adjust(plant);
    if (ledger.pending) adjust(ledger.pending->plant);
    validate(ledger); return changed;
}
void Controller::exclude_offline(WallTime begin,WallTime end) {
    auto next=ledger_;
    if (planters::exclude_offline(next,begin,end)) commit(std::move(next));
}
void Controller::record_travel(const std::string& field,double seconds) {
    if (!name(field) || !std::isfinite(seconds) || seconds<1 || seconds>1800) throw std::invalid_argument("Invalid planter travel sample");
    auto next=ledger_; auto& samples=next.travel[field]; samples.push_back(seconds);
    if (samples.size()>3) samples.erase(samples.begin()); commit(std::move(next));
}
Controller::Controller(std::shared_ptr<Store> store,std::size_t capacity):store_(std::move(store)),capacity_(capacity) {
    if (!store_ || capacity_<1 || capacity_>3) throw std::invalid_argument("Invalid planter controller");
    ledger_=store_->load().value_or(Ledger{}); validate(ledger_);
}
void Controller::clock(WallTime wall) {
    wall_valid(wall);
    if (wall_ && wall<*wall_) throw std::invalid_argument("Planter wall clock moved backwards; reconcile after clock correction");
    wall_=wall;
}
void Controller::commit(Ledger next) {
    validate(next); store_->save(next); ledger_=std::move(next);
}
void Controller::baseline(const ObservationContext& c,const Observation<Placements>& placements,const std::string& present) const {
    if (!placements.has_value(c,Time(2000)) || (present.empty()?!placements.value.complete:!placements.value.identities.contains(present)))
        throw std::runtime_error("Fresh planter presence or a complete placement scan is required");
    scan_valid(placements.value);
    if (!placements.value.complete) return;
    for (const auto& plant : ledger_.plants) if ((placements.value.field.empty() || placements.value.field==plant.stats.field)
        && !placements.value.identities.contains(identity(plant.stats)))
        throw std::runtime_error("A recorded planter is missing; reconcile its state before taking another action");
}
std::uint64_t Controller::place(const Assignment& a,const ObservationContext& c,WallTime wall,const Observation<Placements>& placements,bool attempt) {
    if (ledger_.pending) throw std::logic_error("A planter action is already pending");
    clock(wall);
    const bool observed=placements.has_value(c,Time(2000));
    if (!attempt || (observed && placements.value.complete)) baseline(c,placements);
    else if (observed) scan_valid(placements.value);
    if (observed && !placements.value.field.empty() && placements.value.field!=a.stats.field)
        throw std::runtime_error("Placement observation is from another field");
    auto occupied=observed?placements.value.identities:std::set<std::string>{};
    for (const auto& p : ledger_.plants) occupied.insert(identity(p.stats));
    if (occupied.size()>=capacity_ || ledger_.next_ticket==std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("No planter capacity or action tickets available");
    if (!std::isfinite(a.growth) || a.growth<1 || a.growth>86400 || (a.phase==Phase::build && a.growth<std::round(a.stats.full_seconds)))
        throw std::invalid_argument("Invalid placement growth intent");
    const auto id=identity(a.stats);
    for (const auto& existing : occupied) {
        const auto split=existing.find('/');
        if (existing.substr(0,split)==a.stats.planter || existing.substr(split+1)==a.stats.field) throw std::runtime_error("Planter or field is occupied");
    }
    auto next=ledger_; const auto ticket=next.next_ticket++;
    const auto duration=Time(static_cast<Time::rep>(std::ceil(a.growth*1000)));
    next.pending=Intent{ticket,Operation::place,{ticket,a.stats,a.phase,wall,wall+duration,{},Time{},a.material}};
    Transaction transaction(Operation::place,id,c,placements,attempt);
    commit(std::move(next)); transaction_=std::move(transaction); confirmed_at_.reset(); return ticket;
}
std::uint64_t Controller::harvest(std::uint64_t id,const ObservationContext& c,WallTime wall,const Observation<Placements>& placements,const Observation<Growth>& growth) {
    if (ledger_.pending) throw std::logic_error("A planter action is already pending");
    clock(wall);
    const auto found=std::find_if(ledger_.plants.begin(),ledger_.plants.end(),[&](const Plant& p) { return p.id==id; });
    if (found==ledger_.plants.end() || found->missing || wall<found->placed || wall-found->placed<Time(1000))
        throw std::runtime_error("Planter is unknown or not ready for harvest");
    baseline(c,placements,identity(found->stats));
    if (!placements.value.field.empty() && placements.value.field!=found->stats.field) throw std::runtime_error("Harvest observation is from another field");
    const auto required=harvest_minimum_fraction(*found);
    if (growth.knowledge==Knowledge::unknown) {
        if (wall<ready_at(*found)) throw std::runtime_error("Planter has not reached its saved harvest time");
    } else if (!growth.has_value(c,Time(2000)) || growth.value.identity!=identity(found->stats) || !std::isfinite(growth.value.fraction)
        || growth.value.fraction<required || growth.value.fraction>1
        || std::chrono::abs(placements.captured-growth.captured)>Time(500)) throw std::runtime_error("Planter growth has not reached its harvest intent");
    if (ledger_.next_ticket==std::numeric_limits<std::uint64_t>::max()) throw std::runtime_error("No action tickets available");
    auto next=ledger_; const auto ticket=next.next_ticket++;
    auto& saved=*std::find_if(next.plants.begin(),next.plants.end(),[&](const auto& p) { return p.id==id; });
    if (growth.has_value(c,Time(2000))) saved.growth=GrowthSample{wall,growth.value.fraction,saved.offline};
    next.pending=Intent{ticket,Operation::harvest,saved}; Transaction transaction(Operation::harvest,identity(found->stats),c,placements);
    commit(std::move(next)); transaction_=std::move(transaction); confirmed_at_.reset(); return ticket;
}
WallTime Controller::refresh_growth(std::uint64_t id,const ObservationContext& c,WallTime wall,const Observation<Placements>& placements,const Observation<Growth>& growth) {
    if (ledger_.pending) throw std::logic_error("A planter action is already pending");
    clock(wall);
    auto next=ledger_;
    const auto found=std::find_if(next.plants.begin(),next.plants.end(),[&](const Plant& p) { return p.id==id; });
    if (found==next.plants.end()) throw std::runtime_error("Unknown planter growth identity");
    baseline(c,placements,identity(found->stats));
    if ((!placements.value.field.empty() && placements.value.field!=found->stats.field)
        || !growth.has_value(c,Time(2000)) || growth.value.identity!=identity(found->stats)
        || !std::isfinite(growth.value.fraction) || growth.value.fraction<0 || growth.value.fraction>1
        || std::chrono::abs(placements.captured-growth.captured)>Time(500))
        throw std::runtime_error("Fresh matching planter growth is required");
    const auto observed=wall-(c.now-growth.captured);
    if (observed<found->placed) throw std::runtime_error("Growth predates planter placement");
    if (found->growth && observed<=found->growth->observed) return ready_at(*found);
    const auto fraction=growth.value.fraction;
    found->growth=GrowthSample{observed,fraction,found->offline};
    const auto due=ready_at(*found); commit(std::move(next)); return due;
}
void Controller::finish(bool present,WallTime wall) {
    auto next=ledger_; const auto& intent=*next.pending;
    if (intent.operation==Operation::place && present) {
        auto plant=intent.plant; const auto duration=plant.due-plant.placed;
        plant.placed=wall; plant.due=wall+duration; plant.offline={}; next.plants.push_back(std::move(plant));
        next.fields[intent.plant.stats.field]=wall;
    } else if (intent.operation==Operation::harvest && !present) {
        const bool full=intent.plant.growth && intent.plant.growth->fraction>=1.0;
        material_harvest(next.materials,intent.plant.stats,full,wall.time_since_epoch(),intent.plant.material);
        next.fields[intent.plant.stats.field]=wall;
        std::erase_if(next.plants,[&](const Plant& p) { return p.id==intent.plant.id; });
    }
    next.pending.reset(); commit(std::move(next)); transaction_.reset(); confirmed_at_.reset();
}
Outcome Controller::observe(std::uint64_t ticket,const ObservationContext& c,WallTime wall,const Observation<Placements>& placements,
    const Observation<HarvestEvidence>& harvested) {
    if (!ledger_.pending || ledger_.pending->ticket!=ticket) throw std::invalid_argument("Stale planter action result");
    clock(wall);
    if (!transaction_) return Outcome::uncertain;
    if (placements.has_value(c,Time(2000))) scan_valid(placements.value);
    const auto outcome=transaction_->observe(c,placements,harvested);
    if (outcome==Outcome::confirmed) {
        if (!confirmed_at_) confirmed_at_=wall;
        finish(ledger_.pending->operation==Operation::place,*confirmed_at_);
    } else if (outcome==Outcome::failed) finish(false,wall);
    else if (outcome==Outcome::uncertain) { transaction_.reset(); }
    return outcome;
}
void Controller::input_finished(std::uint64_t ticket,const ObservationContext& context) {
    if (!transaction_ || !ledger_.pending || ledger_.pending->ticket!=ticket) throw std::logic_error("No matching planter action to confirm");
    transaction_->input_finished(context); confirmed_at_.reset();
}
void Controller::abandon_unstarted_placement(std::uint64_t ticket,WallTime wall) {
    if (!ledger_.pending || ledger_.pending->ticket!=ticket || ledger_.pending->operation!=Operation::place
        || !transaction_ || transaction_->input_was_finished())
        throw std::logic_error("Only an unstarted planter placement can be abandoned");
    clock(wall); finish(false,wall);
}
void Controller::reserve_unconfirmed() {
    if (!ledger_.pending) throw std::logic_error("No pending planter action to reserve");
    auto next=ledger_;
    if (next.pending->operation==Operation::place) {
        auto plant=next.pending->plant; plant.missing=true;
        next.plants.push_back(std::move(plant));
    } else {
        for (auto& plant:next.plants) if (plant.id==next.pending->plant.id) plant.missing=true;
    }
    next.pending.reset();
    commit(std::move(next)); interrupt();
}
bool Controller::reconcile_returned(const ObservationContext& c,WallTime wall,const Observation<ReturnedPlanter>& returned) {
    if (transaction_) throw std::logic_error("Cannot reconcile an active planter action");
    if (!returned.has_value(c,Time(2000)) || returned.value.first_seen<Time{} || returned.value.first_seen>returned.captured
        || returned.captured-returned.value.first_seen>Time(2000)) return false;
    const Plant* target=nullptr;
    if (ledger_.pending) target=&ledger_.pending->plant;
    else for (const auto& plant : ledger_.plants) if (identity(plant.stats)==returned.value.identity) target=&plant;
    if (!target || returned.value.identity!=identity(target->stats)) return false;
    const auto& type=target->stats.planter;
    const std::set<std::string> reusable{"PlasticPlanter","CandyPlanter","BlueClayPlanter","RedClayPlanter","TackyPlanter",
        "PesticidePlanter","HeatTreatedPlanter","HydroponicPlanter","PetalPlanter","PlanterOfPlenty"};
    if (!reusable.contains(type)) return false;
    clock(wall);
    if (ledger_.pending) finish(false,wall);
    else {
        auto next=ledger_; const auto id=target->id;
        std::erase_if(next.plants,[&](const Plant& plant) { return plant.id==id; });
        commit(std::move(next));
    }
    return true;
}
void Controller::interrupt() noexcept { transaction_.reset(); confirmed_at_.reset(); }
bool Controller::mark_missing(std::uint64_t id) {
    if (ledger_.pending) throw std::logic_error("Cannot mark a planter missing during an action");
    auto next=ledger_;
    const auto found=std::find_if(next.plants.begin(),next.plants.end(),[&](const Plant& plant) { return plant.id==id; });
    if (found==next.plants.end()) throw std::invalid_argument("Saved planter no longer exists");
    if (found->missing) return false;
    found->missing=true; commit(std::move(next)); return true;
}
}
