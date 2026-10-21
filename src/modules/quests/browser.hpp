#pragma once
#include "tracker.hpp"
#include "core/runtime/program.hpp"
#include <mutex>

namespace hive::features::quests {
struct BrowserScene {
    std::optional<Point> toggle,scroll;
    bool panel_visible{};
    std::string fingerprint;
    std::vector<Observation<Card>> cards;
    bool names_complete{true};
};
class BrowserSource {
public:
    virtual ~BrowserSource()=default;
    virtual Observation<BrowserScene> read() const=0;
};
struct ScanReport {
    std::string status{"Not scanned"};
    unsigned pages{},quests{},readable{},unresolved{};
    bool finished{};
};
struct ScannedQuests {
    ObservationContext context;
    std::vector<Observation<Card>> cards;
    bool reached_end{};
    bool names_complete{true};
};
class ScanResults {
    mutable std::mutex mutex_;
    ScanReport report_;
    std::optional<ScannedQuests> snapshot_;
public:
    ScanReport read() const { std::lock_guard lock(mutex_); return report_; }
    std::optional<ScannedQuests> snapshot(Time after=Time(-1)) const {
        std::lock_guard lock(mutex_);
        return snapshot_ && snapshot_->context.now>after?snapshot_:std::nullopt;
    }
    void complete(ScannedQuests snapshot) { std::lock_guard lock(mutex_); snapshot_=std::move(snapshot); }
    void publish(ScanReport report) { std::lock_guard lock(mutex_); report_=std::move(report); }
};
std::unique_ptr<automation::Program> browser_program(std::shared_ptr<const BrowserSource> source,std::shared_ptr<ScanResults> results,bool close_when_finished=false,std::set<std::string> requested={});
}
