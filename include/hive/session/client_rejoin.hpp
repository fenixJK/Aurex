#pragma once
#include "hive/platform/platform.hpp"
#include "hive/game/data/join.hpp"
#include <set>

namespace hive {
struct RejoinLimits {
    Time close{10000},window{120000},focus{30000},poll{250};
};
class ClientRejoin {
    enum class Stage { closing, launching, window, focused, complete };
    Stage stage_{Stage::closing};
    game::JoinTarget target_;
    RejoinLimits limits_;
    Window old_,candidate_;
    std::set<std::int64_t> excluded_;
    Time deadline_,next_{},last_;
    bool close_sent_{};
public:
    ClientRejoin(game::JoinTarget target,Window old,const std::vector<std::int64_t>& existing,Time now,RejoinLimits limits={});
    std::optional<Window> update(Desktop& desktop,Time now);
    std::string status() const;
};
}
