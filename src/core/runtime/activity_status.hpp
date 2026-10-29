#pragma once
#include <string>

namespace hive {
enum class ActivityKind { status,failure,diagnostic,recovery };
struct ActivityStatus {
    std::string title,detail;
    ActivityKind kind{ActivityKind::status};
    bool operator==(const ActivityStatus&) const = default;
    bool empty() const { return title.empty(); }
    bool is_alert() const { return kind==ActivityKind::recovery || kind==ActivityKind::failure; }
    std::string text() const { return detail.empty()?title:title+" · "+detail; }
};
}
