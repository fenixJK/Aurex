#include "planner.hpp"
#include <algorithm>
#include <functional>
#include <tuple>

namespace hive::features::planters {
namespace {
void number(double value,double low,double high) {
    if (!std::isfinite(value) || value<low || value>high) throw std::invalid_argument("Invalid planter model value");
}
void target(Target t) { number(t.minimum,0,100); number(t.buffer,0,20); }
void validate_events(std::span<const Event> events) {
    if (events.size()>32) throw std::invalid_argument("Too many nectar events");
    for (const auto& e : events) { number(e.seconds,0,7*86400); number(e.amount,0,10000); }
}
std::vector<Event> sorted(std::span<const Event> events) {
    validate_events(events);
    std::vector<Event> result(events.begin(),events.end());
    std::stable_sort(result.begin(),result.end(),[](const Event& a,const Event& b) { return a.seconds<b.seconds; }); return result;
}
double forecast_sorted(double current,std::span<const Event> events,double seconds) {
    double level=current,previous=0;
    for (const auto& event : events) {
        if (event.seconds>seconds) break;
        level=std::min(100.0,std::max(0.0,level-(event.seconds-previous)/864)+event.amount); previous=event.seconds;
    }
    return std::max(0.0,level-(seconds-previous)/864);
}
double floor_time_sorted(double current,std::span<const Event> events,double floor) {
    double level=current,previous=0;
    for (const auto& event : events) {
        const auto crossing=previous+std::max(0.0,level-floor)*864;
        if (crossing<event.seconds) return crossing;
        level=std::min(100.0,std::max(0.0,level-(event.seconds-previous)/864)+event.amount); previous=event.seconds;
    }
    return previous+std::max(0.0,level-floor)*864;
}
double useful_seconds(double rate,double full,Target t) {
    const auto points=std::max(bounds(t).upper-t.minimum,100.0/38);
    return std::min(full,std::max(1.0,std::ceil(points/rate)));
}
double target_time(double current,std::span<const Event> events,double rate,double full,double goal,double earliest) {
    std::vector<double> boundaries{earliest,full};
    for (const auto& e : events) if (e.seconds>earliest && e.seconds<full) boundaries.push_back(e.seconds);
    std::sort(boundaries.begin(),boundaries.end());
    double left=earliest;
    for (double right : boundaries) {
        if (forecast_sorted(current,events,left)+rate*left>=goal) return left;
        if (forecast_sorted(current,events,right)+rate*right>=goal) {
            double low=left,high=right;
            for (int i=0;i<40;++i) {
                const auto middle=(low+high)/2;
                if (forecast_sorted(current,events,middle)+rate*middle>=goal) high=middle; else low=middle;
            }
            return std::min(full,std::ceil(high));
        }
        left=right;
    }
    return full;
}
double interval_sorted(double current,std::span<const Event> events,double rate,double full,Target t,bool building,double delay) {
    full=std::max(1.0,std::round(full)); const auto band=bounds(t);
    if (rate==0 || building || (current<band.lower && t.minimum-band.lower<100.0/38)) return full;
    const auto protected_floor=std::max(0.0,t.minimum*(1-0.75*t.buffer/100));
    const auto useful=useful_seconds(rate,full,t);
    const auto protected_time=std::max(0.0,floor_time_sorted(current,events,protected_floor)-delay);
    if (protected_time>=useful) return std::clamp(std::floor(protected_time),1.0,full);
    const auto hard_time=std::max(0.0,floor_time_sorted(current,events,band.lower)-delay);
    const auto pixel=std::min(full,std::max(1.0,std::ceil((100.0/38)/rate)));
    if (hard_time>=std::min(useful,pixel)) return std::clamp(std::floor(hard_time),1.0,full);
    auto recovery=target_time(current,events,rate,full,band.upper,useful);
    const auto restored=forecast_sorted(current,events,recovery);
    if (restored>=t.minimum) {
        std::vector<Event> later;
        for (const auto& e : events) if (e.seconds>recovery) later.push_back({e.seconds-recovery,e.amount});
        recovery+=std::max(0.0,floor_time_sorted(restored,later,protected_floor)-delay);
    }
    return std::clamp(recovery,1.0,full);
}
}
Bounds bounds(Target t) { target(t); const auto band=t.minimum*t.buffer/100; return {std::max(0.0,t.minimum-band),std::min(100.0,t.minimum+band)}; }
Phase phase(Phase previous,double current,Target t) {
    if (previous<Phase::build || previous>Phase::recover) throw std::invalid_argument("Invalid planter phase");
    number(current,0,100); const auto band=bounds(t);
    if (previous==Phase::build || previous==Phase::recover) return current>=band.upper?Phase::maintain:previous;
    return current<band.lower?Phase::recover:Phase::maintain;
}
double forecast(double current,std::span<const Event> events,double seconds) {
    number(current,0,100); number(seconds,0,7*86400);
    return forecast_sorted(current,sorted(events),seconds);
}
double floor_time(double current,std::span<const Event> events,double floor) {
    number(current,0,100); number(floor,0,100);
    return floor_time_sorted(current,sorted(events),floor);
}
double interval(double current,std::span<const Event> events,double rate,double full,Target t,bool building,double delay) {
    number(current,0,100); number(rate,0,1); number(full,1,86400); number(delay,0,1800); target(t);
    return interval_sorted(current,sorted(events),rate,full,t,building,delay);
}
void validate_configuration(std::span<const Group> groups,std::span<const Active> active,std::size_t capacity) {
    if (groups.size()>5 || capacity>3 || active.size()>3) throw std::invalid_argument("Invalid planter capacity");
    std::set<std::string> busy_types,busy_fields;
    for (const auto& a : active) if (a.planter.empty() || a.field.empty() || !busy_types.insert(a.planter).second || !busy_fields.insert(a.field).second)
        throw std::invalid_argument("Conflicting active planters");
    std::set<Nectar> nectars;
    for (const auto& g : groups) {
        number(g.current,0,100); target(g.target); validate_events(g.pending);
        if (!g.current_known && g.current!=0) throw std::invalid_argument("Unknown nectar must use its conservative lower bound");
        if (g.nectar<Nectar::comforting || g.nectar>Nectar::invigorating || g.phase<Phase::build || g.phase>Phase::recover
            || !nectars.insert(g.nectar).second || g.candidates.size()>512 || g.priority<0 || g.priority>100)
            throw std::invalid_argument("Invalid nectar group");
        std::set<std::string> types;
        for (const auto& c : g.candidates) {
            const auto& s=c.stats;
            if (s.planter.empty() || s.field.empty() || s.nectar!=g.nectar || c.last_used<Time{} || c.last_used>Time::max()-std::chrono::hours(24*31)) throw std::invalid_argument("Invalid planter candidate");
            number(s.nectar_bonus,0.01,10); number(s.speed_bonus,0.01,10); number(s.full_seconds,1,86400); number(c.travel.seconds,0,1800);
            types.insert(s.planter);
        }
        if (types.size()>32) throw std::invalid_argument("Too many planter types");
    }
}
std::vector<Assignment> assign(std::span<const Group> groups,std::span<const Active> active,std::size_t capacity,Selection selection,bool at_destination) {
    validate_configuration(groups,active,capacity);
    if (selection.omitted && (*selection.omitted<Nectar::comforting || *selection.omitted>Nectar::invigorating)) throw std::invalid_argument("Invalid omitted nectar");
    if (active.size()>=capacity) return {};
    std::set<std::string> busy_types,busy_fields;
    for (const auto& a : active) { busy_types.insert(a.planter); busy_fields.insert(a.field); }
    struct Need { double urgency{},reserve{}; int priority{}; Nectar nectar{}; bool repair{},pending{}; std::vector<Assignment> options; };
    std::vector<Need> needs; bool shortage=false;
    double maximum_band=0;
    for (const auto& g : groups) maximum_band=std::max(maximum_band,g.target.minimum-bounds(g.target).lower);
    for (const auto& g : groups) {
        if (g.target.minimum==0) continue;
        std::vector<const Candidate*> eligible,rotated,sipping;
        for (const auto& c : g.candidates) {
            const auto& s=c.stats;
            if (busy_types.contains(s.planter) || busy_fields.contains(s.field)) continue;
            eligible.push_back(&c);
            if (s.field!=g.last_field) rotated.push_back(&c);
            if (g.phase!=Phase::build && s.field==g.sipping_field) sipping.push_back(&c);
        }
        if (!sipping.empty()) eligible=std::move(sipping); else if (!rotated.empty()) eligible=std::move(rotated);
        if (eligible.empty() || selection.omitted==g.nectar) continue;
        std::stable_sort(eligible.begin(),eligible.end(),[](const auto* a,const auto* b) { return a->last_used<b->last_used; });
        const auto ordered=sorted(g.pending);
        auto reserve=g.current; for (const auto& e : g.pending) reserve+=e.amount;
        const auto band=bounds(g.target); shortage=shortage || reserve<band.lower;
        const auto goal=g.phase==Phase::build?band.upper:std::min(100.0,band.lower+2*maximum_band);
        const bool repair=selection.concentrated && (g.phase==Phase::build || (g.phase==Phase::recover && g.current<band.lower));
        const bool pending=repair && std::any_of(ordered.begin(),ordered.end(),[&](const Event& event) { return forecast_sorted(g.current,ordered,event.seconds)>=band.upper; });
        Need need{reserve-goal,reserve,g.priority,g.nectar,repair,pending,{}};
        if (selection.concentrated && !repair) need.urgency=reserve-band.upper-(reserve<band.upper?100:0);
        for (const auto* candidate : eligible) {
            const auto& c=*candidate;
            const auto delay=at_destination?0:c.travel.seconds;
            const auto at_place=forecast_sorted(g.current,ordered,delay);
            std::vector<Event> later;
            for (const auto& e : ordered) if (e.seconds>delay) later.push_back({e.seconds-delay,e.amount});
            auto growth=interval_sorted(at_place,later,c.stats.rate(),c.stats.full_seconds,g.target,!g.current_known || g.phase==Phase::build,c.travel.learned?0:c.travel.seconds);
            growth=std::max(growth,c.travel.seconds);
            const auto ready=delay+growth;
            const auto amount=c.stats.rate()*std::min(growth,c.stats.full_seconds);
            Assignment choice{c.stats,g.current_known?g.phase:Phase::build,growth,ready,std::min(amount,100-forecast_sorted(g.current,ordered,ready))};
            const auto same=std::find_if(need.options.begin(),need.options.end(),[&](const Assignment& a) { return a.stats.planter==c.stats.planter; });
            if (same==need.options.end()) need.options.push_back(choice);
            else if (choice.useful/choice.growth>same->useful/same->growth+1e-12) *same=choice;
        }
        if (need.options.size()>32) throw std::invalid_argument("Too many planter types");
        needs.push_back(std::move(need));
    }
    std::optional<Nectar> focus; int priority=101;
    for (const auto& need : needs) if (need.repair && !need.pending && need.priority<priority) { focus=need.nectar; priority=need.priority; }
    if (selection.concentrated) std::erase_if(needs,[&](const Need& need) { return need.repair && (need.pending || focus!=need.nectar); });
    if (shortage) std::erase_if(needs,[&](const Need& n) {
        const auto g=std::find_if(groups.begin(),groups.end(),[&](const Group& g) { return g.nectar==n.nectar; });
        return n.reserve>=bounds(g->target).upper;
    });
    std::stable_sort(needs.begin(),needs.end(),[](const Need& a,const Need& b) { return std::tie(a.urgency,a.priority)<std::tie(b.urgency,b.priority); });
    const auto slots=capacity>active.size()?capacity-active.size():0;
    std::vector<Assignment> best,chosen;
    std::tuple<std::size_t,unsigned,double,double> best_score{};
    std::function<void(std::size_t,unsigned,double,double)> search=[&](std::size_t index,unsigned coverage,double production,double growth) {
        if (chosen.size()==slots || index==needs.size()) {
            const auto score=std::tuple{chosen.size(),coverage,production,growth};
            if (score>best_score) { best_score=score; best=chosen; } return;
        }
        const auto remaining=std::min(slots-chosen.size(),needs.size()-index);
        auto possible_coverage=coverage;
        for (std::size_t i=0;i<remaining;++i) possible_coverage+=1u<<(needs.size()-index-i-1);
        if (std::tuple{chosen.size()+remaining,possible_coverage}<std::tuple{std::get<0>(best_score),std::get<1>(best_score)}) return;
        for (const auto& a : needs[index].options) {
            if (busy_types.contains(a.stats.planter) || busy_fields.contains(a.stats.field)) continue;
            busy_types.insert(a.stats.planter); busy_fields.insert(a.stats.field); chosen.push_back(a);
            search(index+1,coverage+(1u<<(needs.size()-index-1)),production+a.useful/a.growth,growth+a.growth);
            chosen.pop_back(); busy_types.erase(a.stats.planter); busy_fields.erase(a.stats.field);
        }
        search(index+1,coverage,production,growth);
    };
    search(0,0,0,0); return best;
}
void TravelTimes::record(const std::string& field,double seconds) {
    if (field.empty() || field.size()>80 || (!samples_.contains(field) && samples_.size()>=64)) throw std::invalid_argument("Invalid travel field");
    number(seconds,1,1800); auto& values=samples_[field]; values.push_back(seconds);
    if (values.size()>3) values.erase(values.begin());
}
Travel TravelTimes::estimate(const std::string& field) const {
    const auto found=samples_.find(field); if (found==samples_.end()) return {};
    auto values=found->second; std::sort(values.begin(),values.end()); const auto n=values.size();
    return {n%2?values[n/2]:(values[n/2-1]+values[n/2])/2,true};
}
Transaction::Transaction(Operation op,std::string identity,const ObservationContext& c,const Observation<Placements>& placements,bool attempt)
    :operation_(op),started_(c),identity_(std::move(identity)) {
    if ((op!=Operation::place && op!=Operation::harvest) || identity_.empty() || identity_.size()>160 || !c.generation || !c.window.id || !c.window.bounds.valid() || c.now<Time::zero() || c.now>Time::max()-Time(10000))
        throw std::invalid_argument("Invalid planter transaction");
    const bool placement_attempt=attempt && op==Operation::place;
    const bool observed=placements.has_value(c,Time(2000));
    if ((!observed && !placement_attempt) || (observed && (
        (op==Operation::place && !placements.value.complete && !placement_attempt)
        || (!placements.value.field.empty() && identity_.substr(identity_.find('/')+1)!=placements.value.field)
        || placements.value.identities.contains(identity_)!=(op==Operation::harvest))))
        throw std::invalid_argument("Planter transaction needs a confirmed starting state");
    minimum_=c.now; deadline_=c.now+Time(10000);
}
void Transaction::input_finished(const ObservationContext& c) {
    if (input_finished_ || outcome_!=Outcome::pending || c.generation!=started_.generation || c.window.id!=started_.window.id
        || c.window.process!=started_.window.process || c.window.bounds!=started_.window.bounds || c.now<started_.now || c.now>Time::max()-Time(10000))
        throw std::logic_error("Invalid planter input completion");
    input_finished_=true; minimum_=c.now; deadline_=c.now+Time(10000); last_frame_=Time(-1);
}
Outcome Transaction::observe(const ObservationContext& c,const Observation<Placements>& inventory,const Observation<HarvestEvidence>& harvested) {
    if (outcome_!=Outcome::pending) return outcome_;
    if (c.generation!=started_.generation || c.window.id!=started_.window.id || c.window.process!=started_.window.process
        || c.window.bounds!=started_.window.bounds || c.now<minimum_ || c.now>deadline_) return outcome_=Outcome::uncertain;
    if (!input_finished_) return outcome_;
    if (operation_==Operation::harvest && harvested.has_value(c,Time(2000),minimum_)
        && harvested.value.identity==identity_ && harvested.captured>minimum_
        && harvested.value.latest>=harvested.captured && harvested.value.latest<=c.now
        && !(inventory.has_value(c,Time(2000),minimum_) && inventory.value.identities.contains(identity_))) {
        if (harvested.captured<=last_frame_) return outcome_;
        last_frame_=harvested.value.latest;
        outcome_=Outcome::confirmed;
        return outcome_;
    }
    if (!inventory.has_value(c,Time(2000),minimum_) || (operation_==Operation::harvest && !inventory.value.complete)
        || (!inventory.value.field.empty() && identity_.substr(identity_.find('/')+1)!=inventory.value.field) || inventory.captured<=minimum_) return outcome_;
    if (inventory.captured<=last_frame_) return outcome_;
    last_frame_=inventory.captured;
    const auto present=inventory.value.identities.contains(identity_);
    if ((operation_==Operation::place && present) || (operation_==Operation::harvest && !present)) {
        outcome_=Outcome::confirmed;
    }
    return outcome_;
}
}
