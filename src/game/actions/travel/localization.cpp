#include "localization.hpp"
#include <algorithm>
#include <array>

namespace hive::automation {
namespace {
constexpr std::array prompt_checkpoints{"interaction.prompt","transport.red-cannon"};
}
LandmarkNavigation::LandmarkNavigation(std::shared_ptr<const game::DetectionSource> source):source_(std::move(source)) {
    if (!source_) throw std::invalid_argument("Navigation needs landmark observations");
}
NavigationView LandmarkNavigation::read(const ObservationContext& c,const game::State& state) const {
    std::lock_guard lock(mutex_);
    NavigationView result;
    const auto& banner=state.interaction_banner;
    if (banner.has_value(c,Time(2000))) {
        // Travel observes interaction banners in the shared capture. Reuse its
        // timestamp instead of waiting for the same pixels on another worker.
        const bool ready=banner.value==game::InteractionBanner::ready;
        const Observation<bool> prompt{ready?Knowledge::present:Knowledge::absent,
            ready,banner.captured,banner.window,banner.generation};
        for (const auto id:prompt_checkpoints) result.observations.checkpoints.emplace(id,prompt);
        const bool visible=banner.value!=game::InteractionBanner::none;
        result.observations.checkpoints.emplace("interaction.station",Observation<bool>{visible?Knowledge::present:Knowledge::absent,
            visible,banner.captured,banner.window,banner.generation});
    }
    result.observations.movement_speed=state.movement_speed; result.observations.alive=state.alive; result.observations.connection=state.connection;
    if ((state.alive.has_value(c,Time(2000)) && !state.alive.value)
        || (state.connection.has_value(c,Time(2000)) && state.connection.value==game::Connection::disconnected)) {
        localization_.invalidate(c); return result;
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
                    if (std::find(prompt_checkpoints.begin(),prompt_checkpoints.end(),id)==prompt_checkpoints.end())
                        result.observations.checkpoints.emplace(id,reading);
        }
    }
    return result;
}
}
