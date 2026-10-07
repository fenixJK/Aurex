#pragma once
#include "detection.hpp"

namespace hive::game {
struct ConversationView {
    std::optional<Point> next;
    bool visible{};
};
struct ConversationDetection final : Detection { ConversationView view; };
class ConversationSource {
public:
    virtual ~ConversationSource()=default;
    virtual Observation<ConversationView> read(const ObservationContext&) const=0;
};
std::shared_ptr<const Detector> conversation_detector();
std::shared_ptr<const ConversationSource> conversation_source(std::shared_ptr<const DetectionSource> source);
}
