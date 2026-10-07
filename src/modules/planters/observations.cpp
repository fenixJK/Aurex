#include "hive/modules/planters/observations.hpp"
#include <algorithm>

namespace hive::features::planters {
namespace {
class ObservedSource final : public Source {
    std::shared_ptr<const game::NectarSource> nectar_;
    std::shared_ptr<const Source> field_;
    std::shared_ptr<const game::PlanterSource> planters_;
public:
    ObservedSource(std::shared_ptr<const game::NectarSource> nectar,std::shared_ptr<const Source> field,std::shared_ptr<const game::PlanterSource> planters)
        :nectar_(std::move(nectar)),field_(std::move(field)),planters_(std::move(planters)) {
        if (!nectar_ || !field_) throw std::invalid_argument("Planters needs nectar and field observations");
    }
    Snapshot read(const automation::View& view) const override { return sample(view,field_->read(view)); }
    Snapshot read_at(const automation::View& view,const std::string& field) const override { return sample(view,field_->read_at(view,field)); }
    Snapshot sample(const automation::View& view,Snapshot snapshot) const {
        if (planters_) {
            snapshot.growth={};
            snapshot.rewards=planters_->rewards(view.context);
            snapshot.harvest_prompt=planters_->interaction(view.context);
            const auto visible=planters_->read(view.context);
            const auto& scope=snapshot.placements;
            if (visible.has_value(view.context,Time(2000)) && scope.has_value(view.context,Time(2000)) && !scope.value.field.empty()
                && std::chrono::abs(visible.captured-scope.captured)<=Time(500)) {
                for (const auto& planter : visible.value) {
                    const auto id=planter.type+"/"+scope.value.field;
                    if (!scope.value.identities.contains(id)) continue;
                    if (snapshot.growth.knowledge==Knowledge::present) { snapshot.growth={}; break; }
                    snapshot.growth={Knowledge::present,{id,planter.minimum_percent/100},visible.captured,visible.window,visible.generation};
                }
            }
        }
        snapshot.nectar={};
        const auto reading=nectar_->read(view.context);
        snapshot.nectar_levels=reading;
        if (!reading.has_value(view.context,Time(2000))) return snapshot;
        constexpr std::array mapping{
            std::pair{game::Nectar::comforting,Nectar::comforting},std::pair{game::Nectar::refreshing,Nectar::refreshing},
            std::pair{game::Nectar::satisfying,Nectar::satisfying},std::pair{game::Nectar::motivating,Nectar::motivating},
            std::pair{game::Nectar::invigorating,Nectar::invigorating}};
        std::vector<Group> groups;
        for (const auto& [observed,kind] : mapping) {
            const auto value=reading.value.percent[static_cast<std::size_t>(observed)];
            if (value && (!std::isfinite(*value) || *value<0 || *value>100)) return snapshot;
            Group group; group.nectar=kind; group.current=value.value_or(0); group.current_known=value.has_value();
            for (const auto& stats : catalog()) if (stats.nectar==kind) group.candidates.push_back({stats,{}});
            groups.push_back(std::move(group));
        }
        snapshot.nectar={Knowledge::present,std::move(groups),reading.captured,reading.window,reading.generation};
        return snapshot;
    }
};
class InteractionSource final : public Source {
    std::shared_ptr<const game::PlanterSource> planters_;
public:
    explicit InteractionSource(std::shared_ptr<const game::PlanterSource> planters):planters_(std::move(planters)) {
        if (!planters_) throw std::invalid_argument("Planter interaction observations are missing");
    }
    Snapshot read(const automation::View&) const override { return {}; }
    Snapshot read_at(const automation::View& view,const std::string& field) const override {
        if (field.empty() || std::none_of(catalog().begin(),catalog().end(),[&](const Stats& s) { return s.field==field; }))
            throw std::invalid_argument("Unknown planter visit field");
        Snapshot result;
        const auto prompt=planters_->interaction(view.context);
        if (!prompt.usable(view.context,Time(2000))) return result;
        Placements placements{{},false,field};
        if (prompt.knowledge==Knowledge::present) {
            game::planter_harvest_prompt(prompt.value);
            placements.identities.insert(prompt.value+"/"+field);
        } else if (prompt.knowledge!=Knowledge::absent || !prompt.value.empty()) return result;
        result.placements={Knowledge::present,std::move(placements),prompt.captured,prompt.window,prompt.generation};
        return result;
    }
};
}
Observation<HarvestEvidence> harvest_receipt(const Stats& planter,const Observation<game::NectarLevels>& before,
    const Snapshot& after,const ObservationContext& c,Time minimum) {
    if (planter.nectar<Nectar::comforting || planter.nectar>Nectar::invigorating || minimum<Time{} || minimum>c.now
        || !before.has_value(c,Time(30000)) || before.captured>=minimum
        || !after.nectar_levels.has_value(c,Time(2000),minimum)
        || !after.harvest_prompt.usable(c,Time(2000),minimum) || after.harvest_prompt.knowledge!=Knowledge::absent
        || std::chrono::abs(after.nectar_levels.captured-after.harvest_prompt.captured)>Time(500)) return {};
    const auto id=identity(planter);
    if (after.placements.has_value(c,Time(2000),minimum) && after.placements.value.identities.contains(id)) return {};
    const auto index=static_cast<std::size_t>(planter.nectar);
    const auto& old=before.value; const auto& now=after.nectar_levels.value;
    const auto valid=[](const game::NectarLevels& levels,std::size_t i) {
        return levels.percent[i] && std::isfinite(*levels.percent[i]) && *levels.percent[i]>=0 && *levels.percent[i]<=100
            && std::isfinite(levels.uncertainty_percent) && levels.uncertainty_percent>=0 && levels.uncertainty_percent<=100;
    };
    if (!valid(old,index) || !valid(now,index) || *now.percent[index]-now.uncertainty_percent<=*old.percent[index]+old.uncertainty_percent) return {};
    return {Knowledge::present,{id,std::max(after.nectar_levels.captured,after.harvest_prompt.captured)},
        std::min(after.nectar_levels.captured,after.harvest_prompt.captured),c.window,c.generation};
}
Observation<HarvestEvidence> reward_receipt(const Stats& planter,const Observation<std::vector<game::PlanterReward>>& before,
    const Snapshot& after,const ObservationContext& c,Time minimum) {
    if (planter.nectar<Nectar::comforting || planter.nectar>Nectar::invigorating || minimum<Time{} || minimum>c.now
        || !before.has_value(c,Time(30000)) || before.captured>=minimum
        || !after.rewards.has_value(c,Time(2000),minimum) || after.rewards.captured<=minimum
        || !after.harvest_prompt.usable(c,Time(2000),minimum) || after.harvest_prompt.knowledge!=Knowledge::absent
        || std::chrono::abs(after.rewards.captured-after.harvest_prompt.captured)>Time(500)) return {};
    const auto id=identity(planter);
    if (after.placements.has_value(c,Time(2000),minimum) && after.placements.value.identities.contains(id)) return {};
    for (const auto& reward : after.rewards.value) {
        if (reward.type!=planter.planter || static_cast<int>(reward.nectar)!=static_cast<int>(planter.nectar)
            || reward.text.empty() || std::find(before.value.begin(),before.value.end(),reward)!=before.value.end()) continue;
        return {Knowledge::present,{id,std::max(after.rewards.captured,after.harvest_prompt.captured)},
            std::min(after.rewards.captured,after.harvest_prompt.captured),c.window,c.generation};
    }
    return {};
}
std::shared_ptr<const Source> interaction_source(std::shared_ptr<const game::NectarSource> nectar,std::shared_ptr<const game::PlanterSource> planters) {
    auto field=std::make_shared<InteractionSource>(planters);
    return observed_source(std::move(nectar),std::move(field),std::move(planters));
}
std::shared_ptr<const Source> observed_source(std::shared_ptr<const game::NectarSource> nectar,std::shared_ptr<const Source> field,
    std::shared_ptr<const game::PlanterSource> planters) {
    return std::make_shared<ObservedSource>(std::move(nectar),std::move(field),std::move(planters));
}
}
