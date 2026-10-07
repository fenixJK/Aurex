#pragma once
#include "browser.hpp"
#include "hive/game/detection/conversation.hpp"
#include "hive/game/data/cooldowns.hpp"

namespace hive::features::quests {
struct QuestAction {
    std::function<std::unique_ptr<automation::Program>(const Objective&)> program;
    std::function<bool()> available;
    automation::ProgramFactory prerequisite;
};
struct CyclePlan {
    std::vector<std::string> givers;
    automation::ProgramFactory prepare,return_to_hive;
    std::shared_ptr<const game::ConversationSource> conversation;
    double speed{28};
    std::shared_ptr<game::Cooldowns> cooldowns;
    std::map<std::string,QuestAction> actions;
};
struct GiverStatus {
    std::string quest,status;
    Time next_visit{};
    unsigned completed{},received{};
};
class CycleResults {
    mutable std::mutex mutex_;
    std::map<std::string,GiverStatus> givers_;
public:
    std::map<std::string,GiverStatus> read() const { std::lock_guard lock(mutex_); return givers_; }
    void publish(std::string giver,GiverStatus status) { std::lock_guard lock(mutex_); givers_[std::move(giver)]=std::move(status); }
    void clear() { std::lock_guard lock(mutex_); givers_.clear(); }
};
std::unique_ptr<automation::Program> cycle_program(CyclePlan plan,std::shared_ptr<const BrowserSource> browser,
    std::shared_ptr<ScanResults> scan,std::shared_ptr<CycleResults> results);
}
