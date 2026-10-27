#pragma once
#include "platform/platform.hpp"
#include "game/data/join.hpp"
#include <set>

namespace hive {
struct RejoinLimits {
    Time close{10000},window{120000},focus{30000},poll{250};
    Time retry{3000};
    unsigned attempts{3};
    Time total() const { return close+window*attempts+retry*(attempts-1)+focus; }
};
enum class RejoinMode { restart,join_existing };
class ClientRejoin {
    enum class Stage { closing, launching, window, focused, complete };
    Stage stage_{Stage::closing};
    game::JoinTarget target_;
    RejoinLimits limits_;
    Window old_,candidate_;
    std::set<std::int64_t> excluded_;
    Time deadline_,next_{},last_;
    bool close_sent_{},force_close_sent_{};
    Time force_close_after_{};
    unsigned attempts_{};
    std::future<bool> launch_;
    bool retry_launch(Time now);
public:
    ClientRejoin(game::JoinTarget target,Window old,const std::vector<std::int64_t>& existing,Time now,RejoinLimits limits={},RejoinMode mode=RejoinMode::restart);
    std::optional<Window> update(Desktop& desktop,Time now);
    std::string status() const;
};
}
