#pragma once
#include "core/runtime/model.hpp"
#include <mutex>

namespace hive::extensions {
struct WorkEstimate {
    std::string label,destination;
    Time duration{},ready{};
    std::optional<Time> finish_by;
};
struct WorkWindow { std::string label; Time until{}; };
struct WorkOffer { std::string owner; WorkEstimate work; };
struct ProtectedWork { std::string owner; WorkWindow window; };
struct WorkSnapshot {
    Time observed{};
    std::uint64_t generation{};
    std::vector<WorkOffer> offers;
    std::vector<ProtectedWork> windows;
    const WorkEstimate* find(const std::string& owner) const;
    Time wait_until(const std::string& owner,Time now) const;
    bool can_start_window(const std::string& owner,Time now,Time duration) const;
};
void validate(const WorkEstimate& work);
void validate(const WorkWindow& window);
class WorkSchedule {
    mutable std::mutex mutex_;
    WorkSnapshot snapshot_;
public:
    WorkSnapshot read() const;
    void publish(WorkSnapshot snapshot);
};
}
