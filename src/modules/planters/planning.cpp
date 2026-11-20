#include "planning.hpp"
#include "simulation.hpp"
#include "controller.hpp"
#include <algorithm>
#include <tuple>
#include <iterator>

namespace hive::features::planters {
namespace {
std::vector<Active> active(const PlanningSnapshot& snapshot) {
    std::vector<Active> result;
    for (const auto& p : snapshot.growing) result.push_back({p.stats.planter,p.stats.field,p.material});
    return result;
}
bool building_priorities(std::span<const Group> groups) {
    int highest=101,lowest=0;
    for (const auto& group:groups) if (group.target.minimum>0) {
        highest=std::min(highest,group.priority);lowest=std::max(lowest,group.priority);
    }
    return std::any_of(groups.begin(),groups.end(),[&](const Group& group) {
        return group.target.minimum>0 && (!group.current_known ||
            ((group.priority<lowest || highest==lowest) && group.phase==Phase::build));
    });
}
struct Score { std::vector<double> deficits, shortfall; double travel{}, collections{}; bool maintained{true}; };
Score score(const SimulationResult& result,std::span<const Group> groups,std::array<double,5> ages,double horizon,bool building=true) {
    std::vector<const Group*> enabled;
    for (const auto& group : groups) if (group.target.minimum>0) enabled.push_back(&group);
    std::stable_sort(enabled.begin(),enabled.end(),[](const Group* a,const Group* b) { return a->priority<b->priority; });
    const auto count=enabled.size();
    Score value{{},{},result.travel_seconds,static_cast<double>(result.harvests)};
    std::vector<std::size_t> ranks;
    std::vector<double> resolution;
    for (std::size_t i=0;i<count;++i) {
        if (i==0 || enabled[i]->priority!=enabled[i-1]->priority) { value.deficits.push_back(0); value.shortfall.push_back(0); resolution.push_back(0); }
        ranks.push_back(value.deficits.size()-1);
        resolution.back()+=enabled[i]->uncertainty*864/horizon;
    }
    double established=0;
    for (const auto* group:enabled) if (group->phase==Phase::build) {
        const auto built=result.built_at[static_cast<std::size_t>(group->nectar)];
        if (!built) value.maintained=false; else established=std::max(established,*built);
    }
    // A complete buildup followed by a full nectar reservoir's lifetime of
    // maintenance already meets the goal; shortening it adds needless trips.
    if (horizon-established<100*864) value.maintained=false;
    for (const auto& segment:result.segments) for (const auto* group:enabled) {
        const auto i=static_cast<std::size_t>(group->nectar);
        const auto built=group->phase==Phase::build?result.built_at[i].value_or(horizon):0;
        if (segment.end>built && std::max(0.0,segment.levels[i]-(segment.end-segment.begin)/864)<bounds(group->target).lower-1e-9)
            value.maintained=false;
    }
    // Compare the cost of the nectar actually supplied, not an arbitrary
    // number of trips before the forecast cutoff. Ending with more reserve
    // must not make an otherwise equivalent rotation appear more expensive.
    if (value.maintained) {
        double supplied=0, consumed=0;
        for (const auto* group:enabled) {
            const auto index=static_cast<std::size_t>(group->nectar);
            supplied+=result.final[index]-group->current;
            for (const auto& segment:result.segments)
                consumed+=std::min(segment.end-segment.begin,segment.levels[index]*864)/864;
        }
        supplied+=consumed;
        if (supplied>0) {
            value.travel*=consumed/supplied;
            value.collections*=consumed/supplied;
        }
    }
    std::array<double,5> floors{};
    for (const auto* group:enabled) {
        const bool recovery_waiting=std::any_of(enabled.begin(),enabled.end(),[&](const Group* other) {
            return other->priority>group->priority && other->current<bounds(other->target).lower;
        });
        const auto lower=bounds(group->target).lower;
        floors[static_cast<std::size_t>(group->nectar)]=(!building && !value.maintained && recovery_waiting)
            ?std::min(group->target.minimum,lower+group->uncertainty):lower;
    }
    for (const auto& segment : result.segments) {
        const auto span=segment.end-segment.begin;
        for (std::size_t i=0;i<count;++i) {
            const auto& group=*enabled[i]; const auto index=static_cast<std::size_t>(group.nectar);
            // Once the protected priorities are built, a sustainable buildup
            // of the remaining nectar is enough. Finishing it slightly sooner
            // does not justify more trips for an already sustainable rotation.
            if (!building && value.maintained && group.phase==Phase::build) continue;
            const auto floor=floors[index];
            const auto good=std::min(span,std::max(0.0,segment.levels[index]-floor)*864);
            if (good>0) ages[index]=0;
            const auto below=span-good, gap=ages[index]*below+below*below/2;
            ages[index]+=below;
            value.deficits[ranks[i]]+=below/horizon+2*gap/(horizon*horizon);
            if (!building || group.phase==Phase::recover) {
                const auto start=std::min(floor,segment.levels[index]), productive=std::min(below,start*864);
                value.shortfall[ranks[i]]+=(floor*below-start*productive+productive*productive/1728)/horizon;
            }
        }
    }
    // Include the useful life of nectar already earned at the window's end,
    // whether collected or still growing. Otherwise a harvest just before the
    // cutoff wins over the same nectar collected just after it. No future
    // growth is credited, and the reservoir remains capped at 24 hours.
    if (!building && !value.maintained && !enabled.empty()) for (const auto* group:enabled)
        if (group->priority==enabled.back()->priority) {
            const auto index=static_cast<std::size_t>(group->nectar);
            auto reserve=result.final[index];
            for (const auto& plant:result.growing) if (plant.stats.nectar==group->nectar)
                reserve+=plant.stats.rate()*std::min(plant.age,plant.stats.full_seconds);
            reserve=std::min(100.0,reserve);
            const auto useful=std::min(reserve,floors[index]);
            value.shortfall.back()-=864*(reserve*useful-useful*useful/2)/horizon;
        }
    // During buildup, ignore differences smaller than the meter can resolve.
    // Established higher priorities keep exact deficit comparisons: rounding
    // those to zero would allow a lower-priority crop to buy a maintenance gap.
    // The last priority retains its noise tolerance to avoid needless trips.
    for (std::size_t i=0;i<value.deficits.size();++i) if ((building || i+1==value.deficits.size()) && resolution[i]>0)
        value.deficits[i]=std::round(value.deficits[i]/resolution[i])*resolution[i];
    // Once higher priorities are built, partial nectar at the final priority
    // is useful too. Maximize that reserve before rewarding brief crossings
    // of its target; otherwise intermittent bursts can beat better upkeep.
    if (!building && value.deficits.size()>1) std::swap(value.deficits.back(),value.shortfall.back());
    return value;
}
bool before(const Score& a,const Score& b) {
    // Lower-priority coverage cannot buy a deficit in a higher priority.
    // Once coverage is equal, prefer fewer trips over excess nectar reserve.
    for (std::size_t i=0;i<a.deficits.size();++i) {
        // Maintaining the final target outranks accumulating partial reserves.
        if (i+1==a.deficits.size() && a.maintained!=b.maintained) return a.maintained;
        if (std::abs(a.deficits[i]-b.deficits[i])>1e-9) return a.deficits[i]<b.deficits[i];
        if (std::abs(a.shortfall[i]-b.shortfall[i])>1e-7) return a.shortfall[i]<b.shortfall[i];
    }
    if (std::abs(a.travel-b.travel)>1e-6) return a.travel<b.travel;
    return a.collections<b.collections-1e-9;
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
        snapshot.growing.push_back({plant.stats,plant.phase,age,due,travel.estimate(plant.stats.field),plant.material,plant.growth_limit});
    }
    std::stable_sort(snapshot.growing.begin(),snapshot.growing.end(),[](const Growing& a,const Growing& b) { return a.due_in<b.due_in; });
    for (auto& plant : snapshot.growing) if (plant.due_in<=0) {
        plant.age=std::min(plant.age,std::max(plant.stats.full_seconds,std::round(plant.stats.full_seconds)));
        plant.due_in=0;
    }
    planning_groups(snapshot); return snapshot;
}
std::vector<Growing> harvest_schedule(const PlanningSnapshot& snapshot,std::optional<double> growth_limit) {
    const auto max_growth_seconds=growth_limit.value_or(0);
    if (!std::isfinite(max_growth_seconds) || max_growth_seconds<0 || max_growth_seconds>86400 || (max_growth_seconds>0 && max_growth_seconds<1))
        throw std::invalid_argument("Invalid harvest cadence");
    planning_groups(snapshot); // Validate before changing any intent.
    auto result=snapshot.growing;
    for (auto& plant:result) {
        // Only a joint forecast may shorten buildup. Item goals, unreadable
        // nectar and already-due jobs retain their saved harvest intent.
        if ((!growth_limit && plant.phase==Phase::build) || !plant.material.empty() || plant.due_in<=0) continue;
        auto others=snapshot; others.growing=result;
        std::erase_if(others.growing,[&](const Growing& p) { return p.stats.planter==plant.stats.planter; });
        const auto groups=planning_groups(others);
        const auto group=std::find_if(groups.begin(),groups.end(),[&](const Group& g) { return g.nectar==plant.stats.nectar; });
        if (group==groups.end() || !group->current_known || group->target.minimum==0) continue;
        auto remaining=std::max(1.0-plant.age,interval(group->current,group->pending,plant.stats.rate(),plant.stats.full_seconds,
            group->target,plant.phase==Phase::build,plant.travel.learned?0:plant.travel.seconds,plant.age,plant.travel.seconds,group->uncertainty));
        auto limit=growth_limit?(max_growth_seconds>0?std::min(max_growth_seconds,std::round(plant.stats.full_seconds)):0):plant.growth_limit;
        if (group->max_growth_seconds>0) {
            remaining=std::min(remaining,std::max(0.0,group->max_growth_seconds-plant.age));
            if (limit>0) limit=std::min(limit,group->max_growth_seconds);
        }
        if (limit>0) remaining=std::min(remaining,std::max(0.0,limit-plant.age));
        // A nectar still building or recovering needs its promised delivery.
        // Replanning may bring it forward, never keep postponing the refill.
        if (group->phase!=Phase::maintain) remaining=std::min(remaining,plant.due_in);
        const auto arrival=std::max(plant.travel.seconds,remaining+(plant.travel.learned?0:plant.travel.seconds));
        const auto restored=forecast(group->current,group->pending,arrival)
            +plant.stats.rate()*std::min(plant.stats.full_seconds,plant.age+arrival);
        // Changing the next crop's limit must not bypass meter hysteresis
        // for an existing crop whose harvest is not at either limit. Otherwise
        // repeated rounded readings continually bring its refill forward.
        const bool changed=growth_limit && limit!=plant.growth_limit;
        const bool boundary=changed && ((plant.growth_limit>0 && std::abs(plant.age+plant.due_in-plant.growth_limit)<1)
            || (limit>0 && plant.age+plant.due_in>limit)
            || (limit>0 && std::abs(plant.age+remaining-limit)<1));
        if ((boundary || restored>=group->target.minimum)
            && (boundary || std::abs(plant.due_in-remaining)>std::max(1.0,group->uncertainty*864)))
            plant.due_in=remaining;
        plant.growth_limit=limit;
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
        if (auto materials=material_choices(groups,occupied,snapshot.capacity,decision.selection,snapshot.materials);!materials.empty()) {
            decision.assignments=std::move(materials); decision.selection.max_growth_seconds.reset();
        }
        *this=std::move(next); return decision;
    };
    if (occupied.size()>=snapshot.capacity) { decision.reason=PlanReason::no_capacity; return finish(); }
    int lowest_priority=0;
    for (const auto& group:groups) if (group.target.minimum>0) lowest_priority=std::max(lowest_priority,group.priority);
    const bool building=building_priorities(groups);
    constexpr double horizon=48*3600.;
    std::optional<Score> best, protected_score;
    struct Unprotected {};
    auto preview=[&](Selection selection) {
        SimulationConfig config{snapshot.groups,horizon,snapshot.capacity,snapshot.growing};
        // Forecast physical decay and growth from the current reading.
        // The existing uncertainty reserve protects subsequent readings.
        config.readings=NectarReading::exact;
        config.selection=selection;
        // A candidate that has already lost a protected nectar cannot win.
        // Stop at the first observed violation instead of simulating the rest
        // of its rotation. Use the raw floor, the most permissive score bound.
        if (!building && best && std::all_of(best->deficits.begin(),best->deficits.end()-1,[](double d) { return d==0; }))
            config.choose=[&](const PlanningSnapshot& future) {
                for (const auto& group:future.groups)
                    if (group.target.minimum>0 && group.priority<lowest_priority && group.current+1e-6<bounds(group.target).lower)
                        throw Unprotected{};
                return selection;
            };
        config.record_segments=true; config.max_steps=limits.preview_steps; config.cancelled=limits.cancelled;
        ++decision.previews; return simulate(std::move(config));
    };
    // A chosen duration describes the repeating rotation, including the next
    // placement. Preserving only the existing timers would forecast a different
    // schedule as each slot becomes free.
    double cadence=0;
    for (const auto& plant:snapshot.growing) cadence=std::max(cadence,plant.growth_limit);
    std::vector<Selection> choices{{false,31,cadence},{true,31,cadence}};
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
    for (unsigned mask:masks) choices.push_back({true,mask,cadence});
    bool exhausted=false;
    std::set<std::tuple<bool,unsigned,std::optional<double>>> evaluated;
    const auto evaluate=[&](Selection choice) {
        if (!evaluated.emplace(choice.concentrated,choice.nectars,choice.max_growth_seconds).second) return;
        auto state=snapshot;
        state.growing=harvest_schedule(state,choice.max_growth_seconds);
        // Omitting a covered higher priority cannot work if even all of its
        // existing crops lack enough nectar for the forecast. This optimistic
        // bound ignores overflow, so it only removes impossible schedules.
        if (protected_score && !building && std::all_of(protected_score->deficits.begin(),protected_score->deficits.end()-1,[](double d) { return d==0; }))
            for (const auto& group:groups) if (group.target.minimum>0 && group.priority<lowest_priority && !(choice.nectars&(1u<<static_cast<unsigned>(group.nectar)))) {
                auto available=group.current;
                for (const auto& plant:state.growing) if (plant.stats.nectar==group.nectar)
                    available+=std::min(100.0,plant.stats.rate()*plant.stats.full_seconds);
                if (available<horizon/864+bounds(group.target).lower) return;
            }
        const auto assignments=assign(planning_groups(state),occupied,snapshot.capacity,choice);
        if (assignments.empty()) return;
        SimulationResult result;
        try { result=preview(choice); }
        catch (const Unprotected&) { return; }
        catch (const SimulationLimit&) { exhausted=true; return; }
        const auto candidate=score(result,groups,next.deficit_seconds(),horizon,building);
        if (protected_score) for (std::size_t i=0;i+1<candidate.deficits.size();++i)
            if (candidate.deficits[i]>protected_score->deficits[i]+1e-9
                || (std::abs(candidate.deficits[i]-protected_score->deficits[i])<=1e-9 && candidate.shortfall[i]>protected_score->shortfall[i]+1e-7)) return;
        if (!best || before(candidate,*best)) { best=candidate; decision.selection=choice; decision.assignments=assignments; }
    };
    for (const auto& choice:choices) evaluate(choice);
    protected_score=best;
    if (std::any_of(snapshot.growing.begin(),snapshot.growing.end(),[](const Growing& plant) { return plant.growth_limit>0; }))
        for (auto choice:choices) { choice.max_growth_seconds=0; evaluate(choice); }
    // Compare timings even when the current rotation maintains every nectar:
    // a safe early cadence can still cause needless harvests. Buildup retains
    // its deliveries until the protected targets are built.
    if (!building && best) {
        double maximum=0;
        for (const auto& group:groups) if (group.target.minimum>0) for (const auto& candidate:group.candidates)
            maximum=std::max(maximum,group.max_growth_seconds>0?std::min(candidate.stats.full_seconds,group.max_growth_seconds):candidate.stats.full_seconds);
        // Keep the current cadence in the candidate set, including values
        // found by refinement on the previous placement.
        std::set<double> cadences{maximum};
        for (double seconds=3600;seconds<maximum;seconds*=2) cadences.insert(seconds);
        for (const auto& plant:snapshot.growing) if (plant.growth_limit>0) cadences.insert(std::min(plant.growth_limit,maximum));
        for (double seconds:cadences)
            for (auto choice:choices) { choice.max_growth_seconds=seconds; evaluate(choice); }
        // Harvest ordering makes the score discontinuous: a safe, cheaper
        // rotation can sit beyond an unsafe interval. Sample between the coarse
        // durations before refining one winner, rather than hill-climbing only
        // the first useful duration.
        for (unsigned pass=0;pass<2;++pass) {
            std::vector<double> midpoints;
            for (auto left=cadences.begin(),right=std::next(left);right!=cadences.end();++left,++right)
                if (*right-*left>=2) midpoints.push_back((*left+*right)/2);
            for (double seconds:midpoints) {
                auto choice=decision.selection;choice.max_growth_seconds=seconds;evaluate(choice);
                choice.concentrated=true;choice.nectars=enabled;evaluate(choice);
                cadences.insert(seconds);
            }
        }
        auto step=decision.selection.max_growth_seconds.value_or(0)/2;
        for (unsigned pass=0;pass<6 && step>=1;++pass,step/=2) {
            const auto incumbent=decision.selection;
            const auto center=incumbent.max_growth_seconds.value_or(0);
            for (double seconds:{center-step,center+step}) if (seconds>=1 && seconds<=maximum) {
                auto choice=incumbent;choice.max_growth_seconds=seconds;evaluate(choice);
                choice.concentrated=true;choice.nectars=enabled;evaluate(choice);
            }
        }
    }
    if (!best && exhausted) throw SimulationLimit("Planter schedule search exceeded its work limit");
    decision.reason=decision.assignments.empty()?PlanReason::unavailable:PlanReason::forecast; return finish();
}
}
