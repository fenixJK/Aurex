#include "hive/modules/planters/presentation.hpp"
#include "hive/modules/planters/simulation.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace hive::features::planters {
namespace {
std::string decimal(double value) { std::ostringstream out;out<<std::fixed<<std::setprecision(1)<<value;return out.str(); }
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
        extensions::PlotSeries series{nectar_names()[i],{}};
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
}
