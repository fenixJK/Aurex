#pragma once
#include "core/runtime/model.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <atomic>
#include <condition_variable>

namespace hive::game {
struct HudFeatures;
struct InteractionCaption;
struct Detection { virtual ~Detection()=default; };
struct DetectionInput {
    const Frame& frame;
    std::span<const TextLine> text;
    std::uint64_t generation{};
    std::function<bool()> cancelled;
    std::function<std::vector<TextLine>(const Frame&)> recognize;
    const HudFeatures* hud{};
    const Observation<InteractionCaption>* interaction_caption{};
};
class Detector {
    friend class Observer;
    mutable std::mutex invocation_;
public:
    virtual ~Detector()=default;
    virtual std::vector<Rect> text_regions(const Frame&,std::uint64_t) const { return {{0,0,1,1}}; }
    virtual std::vector<Rect> text_regions(const DetectionInput& input) const { return text_regions(input.frame,input.generation); }
    virtual std::shared_ptr<const Detection> detect(const DetectionInput& input) const=0;
};
struct DetectionResult {
    ObservationContext context;
    std::shared_ptr<const Detection> value;
    std::string error;
};
class DetectionSource {
public:
    virtual ~DetectionSource()=default;
    virtual std::optional<DetectionResult> read() const=0;
    virtual std::optional<DetectionResult> peek() const { return read(); }
};
enum class DetectionMode { pixels, text };
enum class DetectionPriority { normal, responsive };
struct DetectorConfig {
    std::string id;
    std::shared_ptr<const Detector> detector;
    Time interval{750};
    DetectionMode mode{DetectionMode::text};
    bool on_demand{};
    DetectionPriority priority{DetectionPriority::normal};
};
struct DetectionWake { std::condition_variable changed; std::atomic<std::uint64_t> revision{}; };
class Observer;
class DetectionChannel final : public DetectionSource {
    friend class Observer;
    mutable std::mutex mutex_;
    std::optional<DetectionResult> value_;
    std::shared_ptr<DetectionWake> wake_;
    mutable std::atomic<std::int64_t> requested_until_{};
    bool on_demand_{};
    bool wanted(Time now) const { return requested_until_.load()>now.count(); }
    void clear();
    void publish(DetectionResult result);
public:
    std::optional<DetectionResult> read() const override;
    std::optional<DetectionResult> peek() const override;
};
}
