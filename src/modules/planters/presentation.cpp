#include "hive/modules/planters/presentation.hpp"
#include "hive/modules/planters/simulation.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace hive::features::planters {
namespace {
std::string decimal(double value) { std::ostringstream out;out<<std::fixed<<std::setprecision(1)<<value;return out.str(); }
constexpr std::array<const char*,5> colors{"#7e9eb3","#78b375","#b398a7","#937db3","#b35951"};
}
std::shared_ptr<const extensions::Plot> simulation_plot(Preferences preferences,std::size_t capacity,unsigned days,std::shared_ptr<const std::atomic_bool> cancelled) {
    if (days<1 || days>30 || !cancelled) throw std::invalid_argument("Invalid simulation duration");
    validate(preferences);
    SimulationConfig config;config.seconds=days*86400.;config.capacity=capacity;config.record_segments=true;
    config.cancelled=[cancelled] { return cancelled->load(); };
    for(unsigned i=0;i<5;++i) {
        Group group;group.nectar=static_cast<Nectar>(i);
        for(const auto& stats:catalog()) if(stats.nectar==group.nectar) group.candidates.push_back({stats,{}});
        config.groups.push_back(std::move(group));
    }
    apply_preferences(config.groups,preferences);
    AdaptivePlanner planner;
    config.choose=[&](const PlanningSnapshot& snapshot) { return planner.select(snapshot,{4096,config.cancelled}).selection; };
    const auto result=simulate(config);
    auto plot=std::make_shared<extensions::Plot>(); plot->title="Nectar forecast";
    plot->description="From zero nectar with the applied settings. Estimated travel: 90 seconds per trip. Weather, sipping and game delays are not predicted.";
    plot->x_label="Days";plot->y_label="Nectar (%)";plot->x_max=days;
    for(unsigned i=0;i<5;++i) if(preferences.targets[i].minimum>0) {
        extensions::PlotSeries series{nectar_names()[i],{},colors[i]};
        const auto append=[&](std::size_t index) {
            const auto& segment=result.segments[index];
            series.points.push_back({segment.begin/86400.,segment.levels[i]});
            series.points.push_back({segment.end/86400.,std::max(0.,segment.levels[i]-(segment.end-segment.begin)/864)});
        };
        const auto stride=std::max<std::size_t>(1,(result.segments.size()+3999)/4000);
        for(std::size_t j=0;j<result.segments.size();j+=stride) {
            const auto end=std::min(result.segments.size(),j+stride);
            std::size_t low=j,high=j;
            for(std::size_t k=j;k<end;++k) { if(result.segments[k].levels[i]<result.segments[low].levels[i])low=k; if(result.segments[k].levels[i]>result.segments[high].levels[i])high=k; }
            append(std::min(low,high));if(low!=high)append(std::max(low,high));
        }
        series.points.push_back({double(days),result.final[i]});plot->series.push_back(std::move(series));
        plot->statistics.push_back({nectar_names()[i],decimal(result.coverage_seconds[i]/config.seconds*100)+"% coverage, "+(result.built_at[i]?decimal(*result.built_at[i]/3600)+"h to build":"not built")});
    }
    plot->statistics.insert(plot->statistics.begin(),{{"Collections/day",decimal(double(result.harvests)/days)},{"Travel/day",decimal(result.travel_seconds/days/3600)+"h"}});
    return plot;
}
std::shared_ptr<const extensions::Plot> forecast_plot(PlanningSnapshot snapshot,unsigned hours) {
    if (hours<1 || hours>72) throw std::invalid_argument("Choose 1–72 hours");
    planning_groups(snapshot);
    if (snapshot.groups.size()!=5 || std::any_of(snapshot.groups.begin(),snapshot.groups.end(),[](const auto& group) { return !group.current_known; }))
        throw std::invalid_argument("All five nectar readings are needed for a prediction");
    SimulationConfig config; config.groups=snapshot.groups; config.growing=snapshot.growing;
    config.capacity=snapshot.capacity; config.seconds=hours*3600.; config.record_segments=true;
    const auto deadline=monotonic_now()+Time(30000);
    config.cancelled=[deadline] { return monotonic_now()>=deadline; };
    AdaptivePlanner planner;
    config.choose=[&](const PlanningSnapshot& state) { return planner.select(state,{4096,config.cancelled}).selection; };
    const auto result=simulate(config);
    auto plot=std::make_shared<extensions::Plot>(); plot->title="Nectar prediction · next "+std::to_string(hours)+" hours";
    plot->description="Current nectar, saved planters and your settings. Uses recorded travel times (90 seconds where unknown). Weather, sipping and time spent on other work can change the result.";
    plot->x_label="Hours from now"; plot->y_label="Nectar (%)"; plot->x_max=hours;
    for (const auto& group:snapshot.groups) {
        const auto i=static_cast<std::size_t>(group.nectar);
        extensions::PlotSeries series{nectar_names()[i],{},colors[i]};
        for (const auto& segment:result.segments) {
            series.points.push_back({segment.begin/3600.,segment.levels[i]});
            series.points.push_back({segment.end/3600.,std::max(0.,segment.levels[i]-(segment.end-segment.begin)/864)});
        }
        series.points.push_back({double(hours),result.final[i]}); plot->series.push_back(std::move(series));
        auto value=decimal(group.current)+"% now → "+decimal(result.final[i])+"% at "+std::to_string(hours)+"h";
        if (group.target.minimum>0) {
            const auto upper=bounds(group.target).upper;
            value+=" · "+decimal(upper)+"% target: ";
            value+=group.current>=upper?"already reached":result.built_at[i]?decimal(*result.built_at[i]/3600)+"h":"not reached in this window";
        } else value+=" · maintenance off";
        plot->statistics.push_back({nectar_names()[i],std::move(value)});
    }
    return plot;
}
}
