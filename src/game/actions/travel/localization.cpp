#include "localization.hpp"

namespace hive::automation {
LandmarkNavigation::LandmarkNavigation(std::shared_ptr<const game::DetectionSource> source,
    std::map<std::string,std::shared_ptr<const game::LandmarkSource>> checkpoints)
    :source_(std::move(source)),checkpoints_(std::move(checkpoints)) {
    if (!source_ || checkpoints_.size()>32) throw std::invalid_argument("Navigation needs bounded landmark observations");
    for (const auto& [id,reading] : checkpoints_)
        if (id.empty() || id.size()>80 || !reading) throw std::invalid_argument("Invalid navigation checkpoint source");
}
NavigationView LandmarkNavigation::read(const ObservationContext& c,const game::State& state) const {
    std::lock_guard lock(mutex_);
    NavigationView result;
    const auto& banner=state.interaction_banner;
    if (banner.has_value(c,Time(2000))) {
        const bool visible=banner.value!=game::InteractionBanner::none;
        result.observations.checkpoints.emplace("interaction.station",Observation<bool>{visible?Knowledge::present:Knowledge::absent,
            visible,banner.captured,banner.window,banner.generation});
    }
    result.observations.movement_speed=state.movement_speed; result.observations.alive=state.alive; result.observations.connection=state.connection;
    if ((state.alive.has_value(c,Time(2000)) && !state.alive.value)
        || (state.connection.has_value(c,Time(2000)) && state.connection.value==game::Connection::disconnected)) {
        localization_.invalidate(c); return result;
    }
    for (const auto& [id,source] : checkpoints_) {
        const auto reading=source->read(c);
        if (reading.usable(c,Time(2000))) result.observations.checkpoints.emplace(id,reading);
    }
    const auto observation=source_->read();
    result.observations.anchor=localization_.read(c,observation);
    if (observation && observation->error.empty()) {
        const auto& context=observation->context;
        const Observation<bool> frame{Knowledge::present,true,context.now,context.window,context.generation};
        if (!frame.has_value(c,Time(2000))) return result;
        if (const auto* landmarks=dynamic_cast<const game::Landmarks*>(observation->value.get())) {
            if (landmarks->readings.size()>32) return {};
            for (const auto& [id,reading] : landmarks->readings)
                if (!Observation<bool>{Knowledge::present,true,reading.captured,reading.window,reading.generation}.has_value(c,Time(2000))
                    || reading.captured!=observation->context.now) {
                    localization_={}; result.observations.anchor={}; return result;
                }
            unsigned present=0;
            for (const auto& [id,reading] : landmarks->readings) present+=reading.knowledge==Knowledge::present && reading.value;
            result.observations.conflicting_anchors=present>1;
            for (const auto& [id,reading] : landmarks->readings)
                if (reading.usable(c,Time(2000)) && reading.captured==observation->context.now)
                    if (!checkpoints_.contains(id)) result.observations.checkpoints.emplace(id,reading);
        }
    }
    return result;
}
}
