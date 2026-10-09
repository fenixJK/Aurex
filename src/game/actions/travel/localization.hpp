#pragma once
#include "services.hpp"
#include "game/detection/localization.hpp"
#include <mutex>

namespace hive::automation {
class LandmarkNavigation final : public NavigationPerception {
    std::shared_ptr<const game::DetectionSource> source_;
    std::map<std::string,std::shared_ptr<const game::LandmarkSource>> checkpoints_;
    mutable std::mutex mutex_;
    mutable game::Localization localization_;
public:
    explicit LandmarkNavigation(std::shared_ptr<const game::DetectionSource> source,
        std::map<std::string,std::shared_ptr<const game::LandmarkSource>> checkpoints={});
    NavigationView read(const ObservationContext& context,const game::State& state) const override;
};
}
