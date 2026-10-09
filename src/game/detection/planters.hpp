#pragma once
#include "detection.hpp"
#include "localization.hpp"
#include "nectar.hpp"

namespace hive::game {
struct VisiblePlanter {
    std::string type;
    double percent{},minimum_percent{};
    Rect bar;
};
struct PlanterReward {
    std::string type;
    Nectar nectar;
    std::string text;
    bool operator==(const PlanterReward&) const=default;
};
struct PlantersDetection final : Detection {
    Observation<std::vector<VisiblePlanter>> visible;
    Observation<std::string> interaction;
    Observation<std::vector<PlanterReward>> rewards;
};
class PlanterSource {
public:
    virtual ~PlanterSource()=default;
    virtual Observation<std::vector<VisiblePlanter>> read(const ObservationContext& context) const=0;
    virtual Observation<std::vector<PlanterReward>> rewards(const ObservationContext&) const { return {}; }
    virtual Observation<std::string> interaction(const ObservationContext&) const { return {}; }
};
PromptLandmark planter_harvest_prompt(const std::string& type);
Observation<std::vector<PlanterReward>> read_planter_rewards(const DetectionInput& input);
Observation<std::string> read_planter_interaction(const DetectionInput& input);
std::shared_ptr<const LandmarkSource> planter_proximity(std::shared_ptr<const PlanterSource> source,std::string type);
Observation<std::vector<VisiblePlanter>> read_planters(const DetectionInput& input);
std::shared_ptr<const Detector> planter_detector();
std::shared_ptr<const PlanterSource> planter_source(std::shared_ptr<const DetectionSource> source);
}
