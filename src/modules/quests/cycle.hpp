#pragma once
#include "browser.hpp"
#include "game/detection/conversation.hpp"
#include "game/data/cooldowns.hpp"
#include "core/module_host/work_schedule.hpp"

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
    std::shared_ptr<const extensions::WorkSchedule> work;
};
struct GiverStatus {
    std::string quest,status;
    Time next_visit{};
    unsigned completed{},received{};
};
class CycleResults {
    mutable std::mutex mutex_;
    std::map<std::string,GiverStatus> givers_;
    std::map<std::string,std::string> reported_blockers_;
public:
    std::map<std::string,GiverStatus> read() const { std::lock_guard lock(mutex_); return givers_; }
    void publish(std::string giver,GiverStatus status) { std::lock_guard lock(mutex_); givers_[std::move(giver)]=std::move(status); }
    bool blocker_changed(const Card& card,const std::string& message) {
        std::lock_guard lock(mutex_);
        const auto current=message.empty()?std::string{}:card.quest+"\n"+message;
        auto& previous=reported_blockers_[card.giver];
        const bool changed=previous!=current; previous=current;
        return changed && !message.empty();
    }
    void clear() { std::lock_guard lock(mutex_); givers_.clear(); reported_blockers_.clear(); }
};
std::string request_blockers(const Card& card,const extensions::WorkSnapshot& work);
std::unique_ptr<automation::Program> cycle_program(CyclePlan plan,std::shared_ptr<const BrowserSource> browser,
    std::shared_ptr<ScanResults> scan,std::shared_ptr<CycleResults> results);
}
