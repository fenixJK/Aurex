#pragma once
#include "detection.hpp"
#include "localization.hpp"
#include <map>

namespace hive::game {
struct VisibleEnemy {
    std::string name;
    Rect health_bar;
    std::optional<double> health_fraction;
};
struct EnemyDefeat { std::string name; Time seen; std::uint64_t id{}; };
struct EnemyActivity { std::string name,field; };
struct EnemiesDetection final : Detection {
    Observation<std::vector<VisibleEnemy>> visible;
    std::map<std::string,Observation<bool>> defeats;
    std::vector<EnemyDefeat> recent_defeats;
    std::vector<EnemyActivity> activity;
};
class EnemySource {
public:
    virtual ~EnemySource()=default;
    virtual Observation<std::vector<VisibleEnemy>> read(const ObservationContext& context) const=0;
};
std::shared_ptr<const Detector> enemy_detector();
std::shared_ptr<const EnemySource> enemy_source(std::shared_ptr<const DetectionSource> source);
std::shared_ptr<const LandmarkSource> enemy_defeat_source(std::shared_ptr<const DetectionSource> source,const std::string& name);
}
