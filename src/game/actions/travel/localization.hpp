#pragma once
#include "services.hpp"
#include "game/detection/localization.hpp"
#include <mutex>

namespace hive::automation {
class LandmarkNavigation final : public NavigationPerception {
    std::shared_ptr<const game::DetectionSource> source_;
    mutable std::mutex mutex_;
    mutable game::Localization localization_;
public:
    explicit LandmarkNavigation(std::shared_ptr<const game::DetectionSource> source);
    void invalidate_position(const ObservationContext& context) const override;
    NavigationView read(const ObservationContext& context,const game::State& state) const override;
};
}
