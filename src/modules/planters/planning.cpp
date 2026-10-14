#include "planning.hpp"
#include "simulation.hpp"
#include "controller.hpp"
#include <algorithm>
#include <numeric>
#include <tuple>

namespace hive::features::planters {
namespace {
std::vector<Active> active(const PlanningSnapshot& snapshot) {
    std::vector<Active> result;
    for (const auto& p : snapshot.growing) result.push_back({p.stats.planter,p.stats.field,p.material});
    return result;
}
std::size_t support_limit(std::span<const Group> groups,std::size_t capacity) {
    std::map<std::string,double> types;
    std::size_t count=0;
    for (const auto& group : groups) if (group.target.minimum>0) {
        ++count;
        for (const auto& candidate : group.candidates) {
            const auto& s=candidate.stats; types[s.planter]=std::max(types[s.planter],s.nectar_bonus*s.speed_bonus);
        }
    }
    std::vector<double> rates;
    for (const auto& [name,rate] : types) rates.push_back(rate);
    std::sort(rates.begin(),rates.end(),std::greater<>());
    const auto production=std::accumulate(rates.begin(),rates.begin()+std::min(capacity,rates.size()),0.0);
    return std::min(std::max<std::size_t>(1,count?count-1:0),static_cast<std::size_t>(std::max(1.0,std::floor(production))));
}
bool stable(const SimulationResult& result,std::span<const Group> groups,double start,double end) {
    for (const auto& group : groups) if (group.target.minimum>0) {
        double coverage=0;
        for (const auto& segment : result.segments) {
            const auto left=std::max(start,segment.begin), span=std::max(0.0,segment.end-left);
            const auto value=std::max(0.0,segment.levels[static_cast<std::size_t>(group.nectar)]-(left-segment.begin)/864);
            coverage+=std::min(span,std::max(0.0,value-bounds(group.target).lower)*864);
        }
        if (coverage<end-start-1) return false;
    }
    return true;
}
struct Score { double utility{},terminal{}; std::size_t collections{}; };
Score score(const SimulationResult& result,std::span<const Group> groups,std::array<double,5> ages,std::size_t support,double horizon) {
    std::vector<const Group*> enabled;
    for (const auto& group : groups) if (group.target.minimum>0) enabled.push_back(&group);
    std::stable_sort(enabled.begin(),enabled.end(),[](const Group* a,const Group* b) { return a->priority<b->priority; });
    const auto count=enabled.size();
    std::vector<double> weights; double total=0;
    for (std::size_t i=0;i<count;++i) { weights.push_back(std::pow(count-i,0.25)); total+=weights.back(); }
    Score value{0,0,result.harvests};
    for (const auto& segment : result.segments) {
        const auto span=segment.end-segment.begin;
        std::vector<double> covered;
        for (std::size_t i=0;i<count;++i) {
            const auto& group=*enabled[i]; const auto index=static_cast<std::size_t>(group.nectar);
            const auto good=std::min(span,std::max(0.0,segment.levels[index]-bounds(group.target).lower)*864);
            covered.push_back(good);
            if (good>0) ages[index]=0;
            const auto below=span-good, gap=ages[index]*below+below*below/2;
            ages[index]+=below;
            value.utility+=weights[i]*count/total*(good/horizon-2*gap/(horizon*horizon));
        }
        std::sort(covered.begin(),covered.end());
        if (count) value.utility+=count*covered[count-std::min(support,count)]/horizon;
    }
    for (std::size_t i=0;i<count;++i) {
        const auto& group=*enabled[i];
        value.terminal+=weights[i]*count/total*std::min(result.final[static_cast<std::size_t>(group.nectar)],group.target.minimum)/100;
    }
    value.utility-=count*result.travel_seconds/horizon;
    return value;
}
bool before(const Score& a,const Score& b) {
    return std::tie(a.utility,a.terminal)>std::tie(b.utility,b.terminal)
        || (a.utility==b.utility && a.terminal==b.terminal && a.collections<b.collections);
}
}
PlanningSnapshot planning_snapshot(const Ledger& ledger,std::vector<Group> groups,const TravelTimes& travel,
    WallTime wall,double session_seconds,std::size_t capacity) {
    validate(ledger);
    if (ledger.pending) throw std::logic_error("Resolve the pending planter action before planning another");
    if (wall.time_since_epoch()<Time::zero()) throw std::invalid_argument("Invalid planter planning wall clock");
    PlanningSnapshot snapshot{std::move(groups),{},capacity,session_seconds};
    snapshot.materials.history=ledger.materials; snapshot.materials.now=wall.time_since_epoch();
    for (auto& group:snapshot.groups) {
        Time latest{};
        for (auto& candidate:group.candidates) {
            const auto used=ledger.fields.find(candidate.stats.field);
            if (used==ledger.fields.end()) continue;
            candidate.last_used=used->second.time_since_epoch();
            if (candidate.last_used>latest) { latest=candidate.last_used; group.last_field=candidate.stats.field; }
        }
    }
    for (const auto& plant : ledger.plants) {
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
void AdaptivePlanner::reset() noexcept { gaps_={}; now_.reset(); previous_={}; }
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
    for (const auto& group : groups) if (group.target.minimum>0 && group.current_known) {
        const auto index=static_cast<std::size_t>(group.nectar); const auto floor=bounds(group.target).lower;
        double age=0; const auto& old=gaps_[index];
        if (old && old->target.minimum==group.target.minimum && old->target.buffer==group.target.buffer && group.current<floor) {
            const auto elapsed=snapshot.now-old->at;
            age=old->value>=floor?std::max(0.0,elapsed-(old->value-floor)*864):old->age+elapsed;
        }
        next.gaps_[index]=Gap{age,snapshot.now,group.current,group.target};
    }
    const auto occupied=active(snapshot);
    PlanDecision decision; decision.selection={previous_.concentrated,{}};
    decision.assignments=assign(groups,occupied,snapshot.capacity,decision.selection);
    if (decision.assignments.empty()) { decision.selection={}; decision.assignments=assign(groups,occupied,snapshot.capacity); }
    auto finish=[&] {
        if (limits.cancelled && limits.cancelled()) throw std::runtime_error("Planter planning cancelled");
        if (auto materials=material_choices(groups,occupied,snapshot.capacity,decision.selection,snapshot.materials);!materials.empty()) decision.assignments=std::move(materials);
        next.previous_=decision.selection; *this=std::move(next); return decision;
    };
    if (decision.assignments.empty()) { decision.reason=snapshot.growing.size()>=snapshot.capacity?PlanReason::no_capacity:PlanReason::unavailable; return finish(); }
    if (std::any_of(groups.begin(),groups.end(),[](const Group& g) { return g.target.minimum>0 && !g.current_known; })) {
        decision.selection={}; decision.assignments=assign(groups,occupied,snapshot.capacity);
        decision.reason=PlanReason::partial_readings; return finish();
    }
    bool uncovered=false;
    for (const auto& group : groups) if (group.target.minimum>0 && group.current<bounds(group.target).lower) {
        const bool pending=std::any_of(group.pending.begin(),group.pending.end(),[&](const Event& event) {
            return forecast(group.current,group.pending,event.seconds)>=bounds(group.target).lower;
        });
        if (!pending) uncovered=true;
    }
    if (!uncovered) { decision.reason=PlanReason::covered; return finish(); }
    auto preview=[&](Selection selection,double hours) {
        SimulationConfig config{snapshot.groups,hours*3600,snapshot.capacity,snapshot.growing};
        for (auto& group : config.groups) group.current=std::round(group.current*38/100)*100/38;
        config.selection=selection; config.record_segments=true; config.max_steps=limits.preview_steps; config.cancelled=limits.cancelled;
        ++decision.previews; return simulate(std::move(config));
    };
    const auto baseline=preview({},96);
    if (stable(baseline,groups,72*3600,96*3600)) {
        decision.selection={}; decision.assignments=assign(groups,occupied,snapshot.capacity); decision.reason=PlanReason::stable; return finish();
    }
    const auto support=support_limit(groups,snapshot.capacity);
    std::vector<Selection> choices{{false,{}},{true,{}}};
    for (const auto& group : groups) if (group.target.minimum>0 && !group.candidates.empty()) choices.push_back({true,group.nectar});
    std::optional<Score> best;
    for (const auto& choice : choices) {
        const auto assignments=assign(groups,occupied,snapshot.capacity,choice);
        if (assignments.empty()) continue;
        const auto result=preview(choice,48);
        const auto candidate=score(result,groups,next.deficit_seconds(),support,48*3600);
        if (!best || before(candidate,*best)) { best=candidate; decision.selection=choice; decision.assignments=assignments; }
    }
    decision.reason=PlanReason::shortage; return finish();
}
}
