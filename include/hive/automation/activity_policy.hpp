#pragma once
#include "hive/core/model.hpp"
#include <span>

namespace hive::automation {
struct ActivityOption {
    std::string id;
    int priority{};
    Time due{};
};
class ActivityPolicy {
public:
    virtual ~ActivityPolicy() = default;
    virtual std::optional<std::string> choose(std::span<const ActivityOption> options) = 0;
    virtual void completed(const std::optional<std::string>& activity) = 0;
};
}
