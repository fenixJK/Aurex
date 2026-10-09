#pragma once
#include "core/runtime/model.hpp"
#include "core/runtime/settings.hpp"
#include <functional>
#include <map>
#include <set>

namespace hive::game {
struct RequestLimits {
    int priority{100};
    std::size_t count{16};
    Time lifetime{60000};
};
void validate(const RequestLimits& limits);
bool request_identifier(const std::string& value);
struct RequestScope {
    std::uint64_t id{}, generation{};
    bool operator==(const RequestScope&) const = default;
};
struct ChannelRequest {
    std::string id;
    int priority{};
    Time expires{};
    Settings data;
};
struct RequestMessage {
    std::string owner,id;
    int priority{};
    Time expires{};
    std::uint64_t generation{};
    Settings data;
};
using RequestChannels=std::map<std::string,std::vector<ChannelRequest>>;
using RequestInbox=std::map<std::string,std::vector<RequestMessage>>;
using RequestGrants=std::map<std::string,RequestLimits>;
using RequestContracts=std::map<std::string,std::function<void(const Settings&)>>;
struct RequestBatch {
    std::uint64_t generation{},revision{};
    RequestChannels channels;
};
class RequestSource {
public:
    virtual ~RequestSource()=default;
    virtual RequestInbox channels(Time now,std::uint64_t generation) const=0;
};
class ChannelBroker {
    struct Owner { std::string name; RequestGrants grants; RequestInbox messages; std::uint64_t revision{}; };
    RequestContracts contracts_;
    std::map<std::uint64_t,Owner> owners_;
    std::uint64_t generation_{},next_scope_{1};
    Time last_{};
public:
    explicit ChannelBroker(RequestContracts contracts={});
    void configure(RequestContracts contracts);
    void reset(std::uint64_t generation);
    std::uint64_t generation() const { return generation_; }
    RequestScope grant(std::string owner,RequestGrants grants);
    void revoke(RequestScope scope) noexcept;
    bool replace(RequestScope scope,const RequestBatch& batch,Time now);
    RequestInbox snapshot(Time now,std::uint64_t generation) const;
};
}
