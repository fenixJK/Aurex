#pragma once
#include "core/runtime/model.hpp"
#include <span>
#include <set>

namespace hive::automation {
struct ActivityOption {
    std::string id;
    int priority{};
    Time due{};
    Time duration{},now{};
    std::optional<Time> latest_start;
};
class ActivityPolicy {
public:
    virtual ~ActivityPolicy() = default;
    virtual std::optional<std::string> choose(std::span<const ActivityOption> options) = 0;
    virtual void completed(const std::optional<std::string>& activity) = 0;
};
class FairActivityPolicy final : public ActivityPolicy {
    std::set<std::string> served_;
public:
    std::optional<std::string> choose(std::span<const ActivityOption> options) override;
    void completed(const std::optional<std::string>& activity) override;
};
}
