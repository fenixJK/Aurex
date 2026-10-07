#pragma once
#include "../game/movement/field_plans.hpp"
#include "request_channels.hpp"
#include "hive/game/data/join.hpp"
#include <map>
#include <memory>
#include <mutex>

namespace hive::game {
struct FieldRequest {
    std::string id, field;
    int priority{};
    Time expires{};
    std::vector<std::string> alternatives;
    bool complete{};
    unsigned hotbar_slot{};
    bool interrupt{};
};
struct EnemyRequest {
    std::string id, enemy;
    int priority{};
    Time expires{};
    std::string field;
};
struct EnemyGoal {
    std::string owner, id, enemy;
    int priority{};
    Time expires{};
    std::uint64_t generation{};
    std::string field;
};
struct Requests {
    std::uint64_t generation{}, revision{};
    std::vector<FieldRequest> fields;
    std::vector<EnemyRequest> enemies;
    RequestChannels channels;
};
struct ServerGoal { game::JoinTarget target; std::string key; Time expires; bool interrupt{true}; };
std::optional<ServerGoal> server_goal(const RequestInbox& inbox,Time now,std::uint64_t generation);
RequestContracts game_request_contracts();
RequestBatch channel_batch(const Requests& requests);
class RequestBroker {
    ChannelBroker channels_{game_request_contracts()};
public:
    void configure(RequestContracts contracts) { channels_.configure(std::move(contracts)); }
    void reset(std::uint64_t generation) { channels_.reset(generation); }
    std::uint64_t generation() const { return channels_.generation(); }
    RequestScope grant_channels(std::string owner,RequestGrants grants) { return channels_.grant(std::move(owner),std::move(grants)); }
    bool replace_channels(RequestScope scope,const RequestBatch& batch,Time now) { return channels_.replace(scope,batch,now); }
    RequestInbox channels(Time now,std::uint64_t generation) const { return channels_.snapshot(now,generation); }
    RequestScope grant(std::string owner,RequestLimits limits,std::optional<RequestLimits> enemy_limits={});
    void revoke(RequestScope scope) noexcept;
    bool replace(RequestScope scope,const Requests& requests,Time now);
    std::vector<FieldGoal> snapshot(Time now,std::uint64_t generation) const;
    std::vector<EnemyGoal> enemies(Time now,std::uint64_t generation) const;
};
class FieldGoalSource {
public:
    virtual ~FieldGoalSource()=default;
    virtual std::vector<FieldGoal> snapshot(Time now,std::uint64_t generation) const=0;
};
class EnemyGoalSource {
public:
    virtual ~EnemyGoalSource()=default;
    virtual std::vector<EnemyGoal> enemies(Time now,std::uint64_t generation) const=0;
};
class SharedRequests final : public FieldGoalSource, public EnemyGoalSource, public RequestSource {
    mutable std::mutex mutex_;
    RequestBroker broker_;
    Time published_{};
public:
    void configure(RequestContracts contracts);
    RequestScope grant_channels(std::string owner,RequestGrants grants);
    bool replace_channels(RequestScope scope,const RequestBatch& batch,Time now);
    RequestInbox channels(Time now,std::uint64_t generation) const override;
    void reset(std::uint64_t generation);
    std::uint64_t generation() const;
    RequestScope grant(std::string owner,RequestLimits limits,std::optional<RequestLimits> enemy_limits={});
    void revoke(RequestScope scope) noexcept;
    bool replace(RequestScope scope,const Requests& requests,Time now);
    std::vector<FieldGoal> snapshot(Time now,std::uint64_t generation) const override;
    std::vector<EnemyGoal> enemies(Time now,std::uint64_t generation) const override;
};
}
