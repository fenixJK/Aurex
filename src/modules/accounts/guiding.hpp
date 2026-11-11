#pragma once
#include "controller.hpp"
#include "coordination/rotation.hpp"
#include "core/module_host/module.hpp"
#include "game/detection/guiding.hpp"
#include "game/network/servers.hpp"

namespace hive::features::accounts {
class GuidingSync {
    std::shared_ptr<const game::DetectionSource> observations_;
    std::shared_ptr<game::ServerDirectory> servers_;
    coordination::SlotRotation rotation_;
    std::string connection_,command_,target_,request_key_,acknowledged_,location_,status_,server_epoch_;
    Time activation_at_{},active_until_{},arrived_at_{};
    std::uint64_t join_revision_{};
    bool desired_main_{},completed_{},managed_{},away_{};
public:
    bool guiding{},enabled{};
    unsigned slot_limit{5},reserved{};
    GuidingSync(std::shared_ptr<const game::DetectionSource> observations,std::shared_ptr<game::ServerDirectory> servers)
        :observations_(std::move(observations)),servers_(std::move(servers)) {}
    void update(Controller& controller,const coordination::Snapshot& group,const extensions::SessionView& view,game::Requests& requests);
    bool managed() const { return managed_; }
    bool away() const { return away_; }
    const std::string& status() const { return status_; }
    void stop();
};
}
