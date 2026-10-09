#pragma once
#include <string>

namespace hive {
enum class ActivityKind { status,failure };
struct ActivityStatus {
    std::string title,detail;
    ActivityKind kind{ActivityKind::status};
    bool operator==(const ActivityStatus&) const = default;
    bool empty() const { return title.empty(); }
    std::string text() const { return detail.empty()?title:title+" · "+detail; }
};
}
