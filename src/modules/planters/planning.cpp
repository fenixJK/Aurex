#include "planning.hpp"
#include "simulation.hpp"
#include "controller.hpp"
#include <algorithm>
#include <tuple>

namespace hive::features::planters {
namespace {
std::vector<Active> active(const PlanningSnapshot& snapshot) {
    std::vector<Active> result;
    for (const auto& p : snapshot.growing) result.push_back({p.stats.planter,p.stats.field,p.material});
    return result;
}
struct Score { std::vector<double> deficits; double travel{}; std::size_t collections{}; };
Score score(const SimulationResult& result,std::span<const Group> groups,std::array<double,5> ages,double horizon) {
    std::vector<const Group*> enabled;
    for (const auto& group : groups) if (group.target.minimum>0) enabled.push_back(&group);
    std::stable_sort(enabled.begin(),enabled.end(),[](const Group* a,const Group* b) { return a->priority<b->priority; });
    const auto count=enabled.size();
    Score value{{},result.travel_seconds,result.harvests};
    std::vector<std::size_t> ranks;
    std::vector<double> resolution;
    for (std::size_t i=0;i<count;++i) {
        if (i==0 || enabled[i]->priority!=enabled[i-1]->priority) { value.deficits.push_back(0); resolution.push_back(0); }
        ranks.push_back(value.deficits.size()-1);
        resolution.back()+=enabled[i]->uncertainty*864/horizon;
    }
    for (const auto& segment : result.segments) {
        const auto span=segment.end-segment.begin;
        for (std::size_t i=0;i<count;++i) {
            const auto& group=*enabled[i]; const auto index=static_cast<std::size_t>(group.nectar);
            const auto good=std::min(span,std::max(0.0,segment.levels[index]-bounds(group.target).lower)*864);
            if (good>0) ages[index]=0;
            const auto below=span-good, gap=ages[index]*below+below*below/2;
            ages[index]+=below;
            value.deficits[ranks[i]]+=below/horizon+2*gap/(horizon*horizon);
        }
    }
    // Do not trade entire crops over forecast differences smaller than the
    // source meter can resolve. Exact readings retain exact comparisons.
    for (std::size_t i=0;i<value.deficits.size();++i) if (resolution[i]>0)
        value.deficits[i]=std::round(value.deficits[i]/resolution[i])*resolution[i];
    return value;
}
bool before(const Score& a,const Score& b) {
    // Lower-priority coverage cannot buy a deficit in a higher priority.
    // Once coverage is equal, prefer fewer trips over excess nectar reserve.
    for (std::size_t i=0;i<a.deficits.size();++i)
        if (std::abs(a.deficits[i]-b.deficits[i])>1e-9) return a.deficits[i]<b.deficits[i];
    return std::tie(a.travel,a.collections)<std::tie(b.travel,b.collections);
}
}
PlanningSnapshot planning_snapshot(const Ledger& ledger,std::vector<Group> groups,const TravelTimes& travel,
    WallTime wall,double session_seconds,std::size_t capacity) {
    validate(ledger);
    if (ledger.pending) throw std::logic_error("Resolve the pending planter action before planning another");
    if (wall.time_since_epoch()<Time::zero()) throw std::invalid_argument("Invalid planter planning wall clock");
    PlanningSnapshot snapshot{std::move(groups),{},placement_capacity(ledger.placement_limits,capacity),session_seconds};
    snapshot.materials.history=ledger.materials; snapshot.materials.now=wall.time_since_epoch();
    for (auto& group:snapshot.groups) {
        std::erase_if(group.candidates,[&](const Candidate& candidate) { return ledger.placement_limits.occupied_fields.contains(candidate.stats.field); });
        Time latest{};
        for (auto& candidate:group.candidates) {
            const auto used=ledger.fields.find(candidate.stats.field);
            if (used==ledger.fields.end()) continue;
            candidate.last_used=used->second.time_since_epoch();
            if (candidate.last_used>latest) { latest=candidate.last_used; group.last_field=candidate.stats.field; }
        }
    }
    for (const auto& plant : ledger.plants) {
        if (plant.missing) {
            if (snapshot.capacity) --snapshot.capacity;
            for (auto& group:snapshot.groups) std::erase_if(group.candidates,[&](const Candidate& candidate) {
                return candidate.stats.planter==plant.stats.planter || candidate.stats.field==plant.stats.field;
            });
            continue;
        }
        if (wall<plant.placed) throw std::invalid_argument("Saved planter placement is in the future");
        const auto age=estimated_age(plant,wall);
        const auto due=plant.growth?std::max(harvest_fraction(plant),plant.growth->fraction)*plant.stats.full_seconds-age
            :(ready_at(plant)-wall).count()/1000.0;
        snapshot.growing.push_back({plant.stats,plant.phase,age,due,travel.estimate(plant.stats.field),plant.material});
    }
    std::stable_sort(snapshot.growing.begin(),snapshot.growing.end(),[](const Growing& a,const Growing& b) { return a.due_in<b.due_in; });
    for (auto& plant : snapshot.growing) if (plant.due_in<=0) {
        plant.age=std::min(plant.age,std::max(plant.stats.full_seconds,std::round(plant.stats.full_seconds)));
        plant.due_in=0;
    }
    planning_groups(snapshot); return snapshot;
}
std::vector<Growing> harvest_schedule(const PlanningSnapshot& snapshot) {
    planning_groups(snapshot); // Validate before changing any intent.
    auto result=snapshot.growing;
    for (auto& plant:result) {
        // Build and item planters keep their promised growth. Unreadable
        // nectar cannot change a saved timer, and an overdue job stays due.
        if (plant.phase==Phase::build || !plant.material.empty() || plant.due_in<=0) continue;
        auto others=snapshot; others.growing=result;
        std::erase_if(others.growing,[&](const Growing& p) { return p.stats.planter==plant.stats.planter; });
        const auto groups=planning_groups(others);
        const auto group=std::find_if(groups.begin(),groups.end(),[&](const Group& g) { return g.nectar==plant.stats.nectar; });
        if (group==groups.end() || !group->current_known || group->target.minimum==0) continue;
        auto remaining=std::max(1.0-plant.age,interval(group->current,group->pending,plant.stats.rate(),plant.stats.full_seconds,
            group->target,false,plant.travel.learned?0:plant.travel.seconds,plant.age,plant.travel.seconds,group->uncertainty));
        if (group->max_growth_seconds>0) remaining=std::min(remaining,std::max(0.0,group->max_growth_seconds-plant.age));
        const auto arrival=std::max(plant.travel.seconds,remaining+(plant.travel.learned?0:plant.travel.seconds));
        const auto restored=forecast(group->current,group->pending,arrival)
            +plant.stats.rate()*std::min(plant.stats.full_seconds,plant.age+arrival);
        if (restored<group->target.minimum) continue;
        // Recompute from observed reserves and other deliveries. Full growth
        // (or the configured cap) bounds postponement; meter noise cannot churn timers.
        if (std::abs(plant.due_in-remaining)>std::max(1.0,group->uncertainty*864)) plant.due_in=remaining;
    }
    return result;
}
void AdaptivePlanner::reset() noexcept { gaps_={}; now_.reset(); }
std::array<double,5> AdaptivePlanner::deficit_seconds() const {
    std::array<double,5> result{};
    for (std::size_t i=0;i<gaps_.size();++i) if (gaps_[i]) result[i]=gaps_[i]->age;
    return result;
}
PlanDecision AdaptivePlanner::select(const PlanningSnapshot& snapshot,PlanningLimits limits) {
    if (!limits.preview_steps || limits.preview_steps>100000) throw std::invalid_argument("Invalid planner work limit");
    if (limits.cancelled && limits.cancelled()) throw std::runtime_error("Planter planning cancelled");
    const auto groups=planning_groups(snapshot);
    if (now_ && snapshot.now<*now_) throw std::invalid_argument("Planter planning clock moved backwards");
    if (snapshot.materials.mode==Mode::items) {
        reset(); now_=snapshot.now;
        PlanDecision decision;
        decision.assignments=placement_choices(groups,active(snapshot),snapshot.capacity,{},snapshot.materials);
        decision.reason=snapshot.growing.size()>=snapshot.capacity?PlanReason::no_capacity:
            decision.assignments.empty()?PlanReason::unavailable:PlanReason::covered;
        return decision;
    }
    if (snapshot.materials.farms_items()) {
        auto items=material_choices(groups,active(snapshot),snapshot.capacity,{},snapshot.materials);
        if (!items.empty()) {
            reset(); now_=snapshot.now;
            return {std::move(items),{},PlanReason::covered,0};
        }
    }
    auto next=*this; next.now_=snapshot.now; next.gaps_={};
    // Track unmet targets from the same lower bound used for planning. A
    // missing meter must not erase a nectar's wait and keep postponing it.
    for (const auto& group : groups) if (group.target.minimum>0) {
        const auto index=static_cast<std::size_t>(group.nectar); const auto floor=bounds(group.target).lower;
        double age=0; const auto& old=gaps_[index];
        if (old && old->target.minimum==group.target.minimum && old->target.buffer==group.target.buffer && group.current<floor) {
            const auto elapsed=snapshot.now-old->at;
            age=old->value>=floor?std::max(0.0,elapsed-(old->value-floor)*864):old->age+elapsed;
        }
        next.gaps_[index]=Gap{age,snapshot.now,group.current,group.target};
    }
    const auto occupied=active(snapshot);
    PlanDecision decision;
    auto finish=[&] {
        if (limits.cancelled && limits.cancelled()) throw std::runtime_error("Planter planning cancelled");
        if (auto materials=material_choices(groups,occupied,snapshot.capacity,decision.selection,snapshot.materials);!materials.empty()) decision.assignments=std::move(materials);
        *this=std::move(next); return decision;
    };
    if (occupied.size()>=snapshot.capacity) { decision.reason=PlanReason::no_capacity; return finish(); }
    auto preview=[&](Selection selection) {
        SimulationConfig config{snapshot.groups,48*3600.,snapshot.capacity,snapshot.growing};
        // Forecast from the current reading and known decay; do not simulate a
        // second layer of HUD rounding inside each candidate plan.
        config.readings=NectarReading::exact;
        config.selection=selection; config.record_segments=true; config.max_steps=limits.preview_steps; config.cancelled=limits.cancelled;
        ++decision.previews; return simulate(std::move(config));
    };
    std::vector<Selection> choices{{false},{true}};
    unsigned enabled=0;
    for (const auto& group:groups) if (group.target.minimum>0) enabled|=1u<<static_cast<unsigned>(group.nectar);
    std::set<unsigned> masks;
    for (const auto& group:groups) if (group.target.minimum>0) {
        // A currently growing crop can cover a nectar without another planting.
        masks.insert(enabled & ~(1u<<static_cast<unsigned>(group.nectar)));
        unsigned prefix=0;
        for (const auto& other:groups) if (other.target.minimum>0 && other.priority<=group.priority)
            prefix|=1u<<static_cast<unsigned>(other.nectar);
        masks.insert(prefix);
    }
    masks.erase(enabled); masks.erase(0);
    for (unsigned mask:masks) choices.push_back({true,mask});
    std::optional<Score> best;
    for (const auto& choice : choices) {
        const auto assignments=assign(groups,occupied,snapshot.capacity,choice);
        if (assignments.empty()) continue;
        const auto result=preview(choice);
        const auto candidate=score(result,groups,next.deficit_seconds(),48*3600);
        if (!best || before(candidate,*best)) { best=candidate; decision.selection=choice; decision.assignments=assignments; }
    }
    decision.reason=decision.assignments.empty()?PlanReason::unavailable:PlanReason::forecast; return finish();
}
}
