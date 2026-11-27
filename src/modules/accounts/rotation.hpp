#pragma once
#include "controller.hpp"
#include "coordination/rotation.hpp"
#include "core/module_host/module.hpp"
#include "game/detection/guiding.hpp"
#include "game/network/servers.hpp"
#include "coordination/attack.hpp"

namespace hive::features::accounts {
class RotationSync {
    std::shared_ptr<const game::DetectionSource> observations_;
    std::shared_ptr<game::ServerDirectory> servers_;
    coordination::SlotRotation rotation_;
    std::string connection_,command_,target_,request_key_,acknowledged_,location_,status_,server_epoch_;
    Time activation_at_{},active_until_{},arrived_at_{};
    std::uint64_t join_revision_{};
    bool desired_main_{},completed_{},managed_{},away_{};
    coordination::AttackAssist assistance_;
    std::function<Time()> clock_;
    coordination::AttackJob job_;
    Time scheduled_finished_{Time(-1)};
    std::shared_ptr<coordination::AttackAssistState> visit_;
    bool attack_pending_{},main_{},attack_complete_{};
    mutable bool launched_{};
    void select_job(coordination::AttackJob job);
public:
    bool guiding{},attack{},swappable{},enabled{};
    unsigned slot_limit{5},reserved{},saw_slot{};
    RotationSync(std::shared_ptr<const game::DetectionSource> observations,std::shared_ptr<game::ServerDirectory> servers,
        coordination::AttackAssist assistance={},std::function<Time()> clock=[] { return std::chrono::duration_cast<Time>(std::chrono::system_clock::now().time_since_epoch()); })
        :observations_(std::move(observations)),servers_(std::move(servers)),assistance_(std::move(assistance)),clock_(std::move(clock)) {}
    void update(Controller& controller,const coordination::Snapshot& group,const extensions::SessionView& view,game::Requests& requests);
    bool managed() const { return managed_; }
    bool away() const { return away_; }
    const std::string& status() const { return status_; }
    bool pending() const { return attack_pending_ && !launched_; }
    bool protecting() const { return attack_pending_ && visit_ && !visit_->finished; }
    std::string destination() const { const auto it=assistance_.destinations.find(job_.enemy);return it==assistance_.destinations.end()?std::string{}:it->second; }
    std::string activity() const { return "Attack support · "+job_.enemy; }
    std::unique_ptr<automation::Program> program() const;
    void stop();
};
}
