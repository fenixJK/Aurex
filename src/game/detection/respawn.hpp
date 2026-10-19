#pragma once
#include "detection.hpp"

namespace hive::game {
enum class RespawnScreen { menu,confirmation,game,respawning };
struct RespawnView { RespawnScreen screen; std::optional<Point> target; };
class RespawnSource {
public:
    virtual ~RespawnSource()=default;
    virtual Observation<RespawnView> read(const ObservationContext&) const=0;
};
Observation<bool> read_empty_health_bar(const Frame& frame,std::uint64_t generation);
std::shared_ptr<const Detector> respawn_detector();
std::shared_ptr<const RespawnSource> respawn_source(std::shared_ptr<const DetectionSource> source);
}
