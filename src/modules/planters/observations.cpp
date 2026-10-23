#include "observations.hpp"
#include <algorithm>

namespace hive::features::planters {
namespace {
class ObservedSource final : public Source {
    std::shared_ptr<const game::NectarSource> nectar_;
    std::shared_ptr<const game::LandmarkSource> harvest_prompt_,placement_visible_,growth_bar_;
public:
    ObservedSource(std::shared_ptr<const game::NectarSource> nectar,
        std::shared_ptr<const game::LandmarkSource> harvest_prompt,std::shared_ptr<const game::LandmarkSource> placement_visible,
        std::shared_ptr<const game::LandmarkSource> growth_bar)
        :nectar_(std::move(nectar)),harvest_prompt_(std::move(harvest_prompt)),placement_visible_(std::move(placement_visible)),growth_bar_(std::move(growth_bar)) {
        if (!nectar_ || !harvest_prompt_ || !placement_visible_ || !growth_bar_)
            throw std::invalid_argument("Planters needs nectar, bar and interaction observations");
    }
    Snapshot read(const automation::View& view) const override { return sample(view,{}); }
    Snapshot read_at(const automation::View& view,const Stats& expected) const override {
        Snapshot snapshot;
        const auto prompt=harvest_prompt_->read(view.context);
        snapshot.harvest_prompt=prompt;
        if (prompt.usable(view.context,Time(2000))) {
            // The saved placement supplies identity. The E/Harvest banner only
            // confirms that this visit has reached an interactable planter.
            Placements local{{},false,expected.field};
            if (prompt.has_value(view.context,Time(2000)) && prompt.value) local.identities.insert(identity(expected));
            snapshot.placements={Knowledge::present,std::move(local),prompt.captured,prompt.window,prompt.generation};
        }
        snapshot.growth_bar=growth_bar_->read(view.context);
        return sample(view,std::move(snapshot));
    }
    Observation<bool> placement_visible(const automation::View& view) const override { return placement_visible_->read(view.context); }
    Snapshot sample(const automation::View& view,Snapshot snapshot) const {
        snapshot.nectar={};
        const auto reading=nectar_->read(view.context);
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
}
Observation<HarvestEvidence> harvest_receipt(const Stats& planter,bool had_growth_bar,
    const Snapshot& after,const automation::View& view,Time minimum) {
    const auto& c=view.context;
    const auto* state=view.observations;
    // Dialogs hide world UI too. Only inspect disappearance in a live view
    // after the interaction, once the confirmation dialog is closed.
    if (!state || !state->game_hud.has_value(c,Time(2000),minimum) || !state->game_hud.value
        || !state->confirmation.usable(c,Time(2000),minimum) || state->confirmation.knowledge!=Knowledge::absent) return {};
    const auto gone=[&](const Observation<bool>& reading) {
        return reading.usable(c,Time(2000),minimum) && reading.knowledge==Knowledge::absent;
    };
    const auto* disappearance=gone(after.harvest_prompt)?&after.harvest_prompt:
        had_growth_bar && gone(after.growth_bar)?&after.growth_bar:nullptr;
    if (!disappearance) return {};
    const auto earliest=std::min({disappearance->captured,state->game_hud.captured,state->confirmation.captured});
    const auto latest=std::max({disappearance->captured,state->game_hud.captured,state->confirmation.captured});
    return {Knowledge::present,{identity(planter),latest},earliest,c.window,c.generation};
}
std::shared_ptr<const Source> observed_source(std::shared_ptr<const game::NectarSource> nectar,
    std::shared_ptr<const game::LandmarkSource> harvest_prompt,std::shared_ptr<const game::LandmarkSource> placement_visible,
    std::shared_ptr<const game::LandmarkSource> growth_bar) {
    return std::make_shared<ObservedSource>(std::move(nectar),std::move(harvest_prompt),std::move(placement_visible),std::move(growth_bar));
}
}
