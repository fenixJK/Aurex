#pragma once
#include "detection.hpp"
#include "landmarks.hpp"
#include "text.hpp"
#include <map>
#include <variant>

namespace hive::game {
struct PromptAnchor {
    std::string id;
    std::variant<PromptLandmark,TextTarget> prompt;
    std::optional<TextTarget> status;
};
struct Landmarks : Detection {
    std::map<std::string,Observation<bool>> readings;
};
class LandmarkSource {
public:
    virtual ~LandmarkSource()=default;
    virtual Observation<bool> read(const ObservationContext& context) const=0;
};
std::shared_ptr<const LandmarkSource> landmark_source(std::shared_ptr<const DetectionSource> source,std::string id);
std::vector<PromptAnchor> interaction_anchors();
std::shared_ptr<const Detector> landmark_detector(std::vector<PromptAnchor> anchors);
std::shared_ptr<const Detector> interaction_prompt_detector();
class Localization {
    Observation<std::string> confirmed_;
    Time processed_{Time(-1)},last_{};
    ObservationContext context_;
public:
    void invalidate(const ObservationContext& context);
    Observation<std::string> read(const ObservationContext& context,const std::optional<DetectionResult>& observation);
};
}
