#pragma once
#include "game/detection/perception.hpp"
#include "game/detection/detection.hpp"
#include "game/movement/movement.hpp"
#include <atomic>
#include <map>
#include "platform/platform.hpp"
#include "game/detection/visual_labels.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace hive::game {
struct ObservedState { State state; std::string error; Time processing{}; };
class Observer {
public:
    explicit Observer(Desktop& desktop);
    ~Observer();
    std::map<std::string,std::shared_ptr<const DetectionSource>> configure(std::vector<DetectorConfig> detectors);
    void configure_speed(std::shared_ptr<const MovementSpeedSource> source);
    bool has_detectors() const;
    void watch(const ObservationContext& context,ObservationNeeds needs={});
    void request_text(std::vector<Rect> regions);
    void request_labels(std::vector<FixedLabelQuery> queries);
    void stop();
    ObservedState latest(bool request_speed=false);
    std::shared_ptr<const Frame> latest_frame() const;
private:
    Desktop& desktop_;
    mutable std::mutex mutex_;
    std::shared_ptr<DetectionWake> wake_{std::make_shared<DetectionWake>()};
    std::optional<ObservationContext> target_;
    ObservedState latest_;
    Reading<double> speed_observation_;
    std::shared_ptr<const MovementSpeedSource> speed_source_;
    std::atomic<std::uint64_t> revision_{};
    std::atomic<std::uint64_t> text_revision_{};
    struct Entry { DetectorConfig config; std::shared_ptr<DetectionChannel> channel; Time next{}; std::optional<ObservationKind> observation; Time captured{Time(-1)}; };
    bool wanted(const Entry& entry,Time now) const;
    std::vector<Entry> detectors_;
    std::shared_ptr<const Frame> frame_;
    std::uint64_t frame_sequence_{};
    std::shared_ptr<const Frame> responsive_frame_;
    std::uint64_t responsive_sequence_{};
    ObservationNeeds needs_;
    std::vector<Rect> text_regions_;
    std::vector<FixedLabelQuery> label_queries_;
    std::uint64_t label_revision_{};
    Time ocr_next_{},labels_next_{};
    std::string capture_error_,labels_error_;
    void clear_detections();
    bool exiting_{};
    std::thread capture_worker_,pixel_worker_,responsive_worker_,controls_worker_,text_worker_;
    void capture();
    void detect(DetectionMode mode,DetectionPriority priority=DetectionPriority::normal);
};
}
