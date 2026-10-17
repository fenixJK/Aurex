#include "simulation.hpp"
#include "drops.hpp"
#include <algorithm>

namespace hive::features::planters {
namespace {
void bounded(double value,double low,double high) {
    if (!std::isfinite(value) || value<low || value>high) throw std::invalid_argument("Invalid planter forecast state");
}
std::vector<Active> occupied(std::span<const Growing> growing) {
    std::vector<Active> active;
    for (const auto& plant : growing) active.push_back({plant.stats.planter,plant.stats.field,plant.material});
    return active;
}
}
std::vector<Group> planning_groups(const PlanningSnapshot& snapshot) {
    bounded(snapshot.now,0,1e12);
    if (snapshot.capacity>3 || snapshot.growing.size()>3) throw std::invalid_argument("Invalid forecast capacity");
    for (const auto& group : snapshot.groups) if (!group.pending.empty()) throw std::invalid_argument("Forecast events must come from growing planters");
    auto groups=snapshot.groups;
    auto queue=snapshot.growing;
    for (const auto& plant : queue) {
        const auto& s=plant.stats;
        bounded(s.nectar_bonus,0.01,10); bounded(s.speed_bonus,0.01,10); bounded(s.full_seconds,1,86400);
        bounded(plant.age,0,30*86400); bounded(plant.due_in,-30*86400,86400); bounded(plant.travel.seconds,0,1800);
        bounded(plant.age+plant.due_in,1-1e-7,86400+1e-7);
        if (s.nectar<Nectar::comforting || s.nectar>Nectar::invigorating || plant.phase<Phase::build || plant.phase>Phase::recover
            || (plant.phase==Phase::build && plant.age+plant.due_in+1e-7<std::round(s.full_seconds))) throw std::invalid_argument("Invalid growing planter intent");
    }
    std::stable_sort(queue.begin(),queue.end(),[](const Growing& a,const Growing& b) { return a.due_in<b.due_in; });
    double free_at=0;
    for (const auto& plant : queue) {
        const auto depart=std::max(free_at,plant.due_in-(plant.travel.learned?plant.travel.seconds:0));
        const auto arrival=std::max(plant.due_in,depart+plant.travel.seconds); free_at=arrival;
        const auto group=std::find_if(groups.begin(),groups.end(),[&](const Group& g) { return g.nectar==plant.stats.nectar; });
        if (group!=groups.end()) group->pending.push_back({arrival,plant.stats.rate()*std::min(plant.stats.full_seconds,plant.age+arrival)});
    }
    validate_configuration(groups,occupied(snapshot.growing),snapshot.capacity);
    for (auto& group : groups) group.phase=group.current_known?phase(group.phase,group.current,group.target):Phase::build;
    return groups;
}
SimulationResult simulate(SimulationConfig config) {
    bounded(config.seconds,1,30*86400);
    if (!config.max_steps || config.max_steps>100000 || (config.readings!=NectarReading::exact && config.readings!=NectarReading::hud)) throw std::invalid_argument("Invalid simulation work limit or reading model");
    planning_groups({config.groups,config.growing,config.capacity,0});
    for (auto& group : config.groups) group.current_known=true;
    struct Job { Growing plant; double start{}, due{}; };
    std::vector<Job> jobs;
    for (const auto& plant : config.growing) jobs.push_back({plant,-plant.age,plant.due_in});
    SimulationResult result; double now=0;
    Time history_origin{};
    for (const auto& group:config.groups) for (const auto& candidate:group.candidates) history_origin=std::max(history_origin,candidate.last_used);
    const auto used=[&](const std::string& field) {
        for (auto& group:config.groups) for (auto& candidate:group.candidates)
            if (candidate.stats.field==field) candidate.last_used=history_origin+Time(static_cast<Time::rep>(now*1000));
    };
    auto observed=[&](double level) { return config.readings==NectarReading::exact?level:std::round(std::floor(level*38/100+1e-9)*100/38); };
    auto update_phases=[&] {
        for (auto& group : config.groups) {
            group.phase=phase(group.phase,observed(group.current),group.target);
            const auto index=static_cast<std::size_t>(group.nectar);
            if (group.target.minimum>0 && !result.built_at[index] && group.current>=bounds(group.target).upper) result.built_at[index]=now;
        }
    };
    auto advance=[&](double until,bool travel) {
        until=std::min(until,config.seconds);
        if (until<now) throw std::logic_error("Planter forecast moved backwards");
        const auto elapsed=until-now;
        Segment segment{now,until,{}};
        for (auto& group : config.groups) {
            const auto index=static_cast<std::size_t>(group.nectar);
            segment.levels[index]=group.current;
            const auto floor=bounds(group.target).lower;
            result.coverage_seconds[index]+=floor==0?elapsed:std::min(elapsed,std::max(0.0,group.current-floor)*864);
            group.current=std::max(0.0,group.current-elapsed/864);
        }
        if (elapsed>0 && config.record_segments) result.segments.push_back(segment);
        if (travel) result.travel_seconds+=elapsed;
        now=until; update_phases();
    };
    update_phases();
    std::size_t iterations=0;
    while (now<=config.seconds) {
        if (config.cancelled && config.cancelled()) throw std::runtime_error("Planter simulation cancelled");
        if (++iterations>config.max_steps) throw std::runtime_error("Planter simulation exceeded its work limit");
        std::stable_sort(jobs.begin(),jobs.end(),[](const Job& a,const Job& b) { return a.due<b.due; });
        const auto departure=jobs.empty()?config.seconds+1:std::max(now,jobs.front().due-(jobs.front().plant.travel.learned?jobs.front().plant.travel.seconds:0));
        if (!jobs.empty() && departure<=now) {
            const auto job=jobs.front();
            const auto arrival=std::max(job.due,now+job.plant.travel.seconds);
            advance(arrival,true);
            if (arrival>config.seconds) break;
            auto group=std::find_if(config.groups.begin(),config.groups.end(),[&](const Group& g) { return g.nectar==job.plant.stats.nectar; });
            if (group!=config.groups.end()) group->current=std::min(100.0,group->current+job.plant.stats.rate()*std::min(job.plant.stats.full_seconds,now-job.start));
            const auto previous=config.materials.history;
            material_harvest(config.materials.history,job.plant.stats,now-job.start+1e-7>=job.plant.stats.full_seconds,config.materials.now+Time(static_cast<Time::rep>(now*1000)),job.plant.material);
            for (const auto& [id,progress]:config.materials.history) {
                const auto old=previous.find(id);
                if (!id.starts_with("item:") && progress.ready>(old==previous.end()?Time{}:old->second.ready)) ++result.bonuses[id];
            }
            if(now-job.start+1e-7>=job.plant.stats.full_seconds) {
                for(const auto& item:item_goals()) result.base_items[item.id]+=item_source(item.id,job.plant.stats).minimum;
                const auto goal=material_goal(job.plant.material);if(!goal.empty()) ++result.item_harvests[goal];
            }
            used(job.plant.stats.field); jobs.erase(jobs.begin()); ++result.harvests; update_phases(); continue;
        }
        if (now>=config.seconds) break;
        if (jobs.size()<config.capacity) {
            PlanningSnapshot snapshot{config.groups,{},config.capacity,now};
            for (auto& group : snapshot.groups) group.current=observed(group.current);
            for (const auto& job : jobs) {
                auto plant=job.plant; plant.age=now-job.start; plant.due_in=job.due-now; snapshot.growing.push_back(plant);
            }
            snapshot.materials=config.materials; snapshot.materials.now+=Time(static_cast<Time::rep>(now*1000));
            const auto selection=config.choose && snapshot.materials.needs_nectar()?config.choose(snapshot):config.selection;
            const auto groups=planning_groups(snapshot);
            auto choices=placement_choices(groups,occupied(snapshot.growing),config.capacity,selection,snapshot.materials);
            if (!choices.empty()) {
                const auto& choice=choices.front();
                const auto group=std::find_if(config.groups.begin(),config.groups.end(),[&](const Group& g) { return g.nectar==choice.stats.nectar; });
                const auto candidate=std::find_if(group->candidates.begin(),group->candidates.end(),[&](const Candidate& c) {
                    return c.stats.field==choice.stats.field && c.stats.planter==choice.stats.planter;
                });
                const auto travel=candidate->travel;
                if (now+travel.seconds>departure) { advance(departure,false); continue; }
                const auto arrival=now+travel.seconds;
                advance(arrival,true); if (arrival>=config.seconds) break;
                jobs.push_back({{choice.stats,choice.phase,0,choice.growth,travel,choice.material},now,now+choice.growth});
                group->last_field=choice.stats.field; used(choice.stats.field); ++result.placements; continue;
            }
        }
        auto wake=std::min(config.seconds,departure);
        if (jobs.size()<config.capacity) if (const auto ready=next_material_ready(config.materials,config.materials.now+Time(static_cast<Time::rep>(now*1000))))
            wake=std::min(wake,(*ready-config.materials.now).count()/1000.);
        advance(wake,false);
    }
    for (const auto& g : config.groups) result.final[static_cast<std::size_t>(g.nectar)]=g.current;
    for (const auto& job : jobs) {
        auto plant=job.plant; plant.age=now-job.start; plant.due_in=job.due-now; result.growing.push_back(plant);
    }
    return result;
}
}
