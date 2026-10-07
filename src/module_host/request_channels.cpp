#include "hive/module_host/request_channels.hpp"
#include <algorithm>
#include <tuple>

namespace hive::game {
bool request_identifier(const std::string& value) {
    return !value.empty() && value.size()<=80
        && std::none_of(value.begin(),value.end(),[](unsigned char c) { return c<32 || c==127; });
}
void validate(const RequestLimits& limits) {
    if (limits.priority<0 || limits.priority>100 || limits.count<1 || limits.count>32
        || limits.lifetime<Time(1) || limits.lifetime>std::chrono::hours(24)) throw std::invalid_argument("Invalid request grant");
}
namespace {
void validate_data(const Settings& data) {
    if (data.size()>16) throw std::invalid_argument("Too many request fields");
    for (const auto& [key,value] : data) {
        if (!request_identifier(key)) throw std::invalid_argument("Invalid request field name");
        const auto text=[](const std::string& s) {
            if (s.size()>160 || std::any_of(s.begin(),s.end(),[](unsigned char c) { return c<32 || c==127; }))
                throw std::invalid_argument("Invalid request text");
        };
        if (const auto* s=std::get_if<std::string>(&value)) text(*s);
        if (const auto* list=std::get_if<SettingSelection>(&value)) {
            if (list->size()>64) throw std::invalid_argument("Request list exceeds limits");
            for (const auto& s : *list) text(s);
        }
    }
}
}
ChannelBroker::ChannelBroker(RequestContracts contracts) { configure(std::move(contracts)); }
void ChannelBroker::configure(RequestContracts contracts) {
    if (generation_ || !owners_.empty()) throw std::logic_error("Stop request publication before replacing contracts");
    if (contracts.size()>64) throw std::invalid_argument("Too many request contracts");
    for (const auto& [id,validate] : contracts)
        if (!request_identifier(id) || !validate) throw std::invalid_argument("Invalid request contract");
    contracts_=std::move(contracts);
}
void ChannelBroker::reset(std::uint64_t generation) { owners_.clear(); generation_=generation; last_={}; }
RequestScope ChannelBroker::grant(std::string name,RequestGrants grants) {
    if (!generation_ || !request_identifier(name) || owners_.size()>=128 || grants.size()>64)
        throw std::invalid_argument("Invalid request owner");
    for (const auto& [_,owner] : owners_) if (owner.name==name) throw std::invalid_argument("Duplicate request owner");
    for (const auto& [channel,limits] : grants) {
        if (!contracts_.contains(channel)) throw std::invalid_argument("Unregistered request contract: "+channel);
        validate(limits);
    }
    const auto id=next_scope_++;
    owners_.emplace(id,Owner{std::move(name),std::move(grants),{},0}); return {id,generation_};
}
void ChannelBroker::revoke(RequestScope scope) noexcept { if (scope.generation==generation_) owners_.erase(scope.id); }
bool ChannelBroker::replace(RequestScope scope,const RequestBatch& batch,Time now) {
    const auto found=owners_.find(scope.id);
    if (!generation_ || scope.generation!=generation_ || batch.generation!=generation_ || found==owners_.end()) return false;
    if (!batch.revision) throw std::invalid_argument("Request snapshot needs a revision");
    if (batch.revision<=found->second.revision) return false;
    if (now<last_ || now<Time{}) throw std::invalid_argument("Request clock moved backwards");
    if (batch.channels.size()>64) throw std::invalid_argument("Too many request channels");
    RequestInbox replacement; std::size_t total{};
    const auto& owner=found->second;
    for (const auto& [channel,requests] : batch.channels) {
        const auto grant=owner.grants.find(channel);
        if (grant==owner.grants.end()) throw std::invalid_argument("Request channel was not granted: "+channel);
        const auto& limits=grant->second;
        if (requests.size()>limits.count) throw std::invalid_argument("Request count exceeds grant");
        std::set<std::string> ids;
        for (const auto& request : requests) {
            if (!request_identifier(request.id) || !ids.insert(request.id).second || request.priority<0
                || request.priority>limits.priority || request.expires<Time{}) throw std::invalid_argument("Invalid request identity, priority or expiry");
            validate_data(request.data); contracts_.at(channel)(request.data);
            if (request.expires<=now) continue;
            if (request.expires-now>limits.lifetime) throw std::invalid_argument("Request lifetime exceeds grant");
            replacement[channel].push_back({owner.name,request.id,request.priority,request.expires,generation_,request.data}); ++total;
        }
    }
    for (const auto& [id,other] : owners_) if (id!=scope.id)
        for (const auto& [_,messages] : other.messages)
            total+=std::count_if(messages.begin(),messages.end(),[&](const auto& m) { return m.expires>now; });
    if (total>256) throw std::invalid_argument("Session request limit exceeded");
    found->second.messages=std::move(replacement); found->second.revision=batch.revision; last_=now; return true;
}
RequestInbox ChannelBroker::snapshot(Time now,std::uint64_t generation) const {
    if (!generation || generation!=generation_) return {};
    if (now<last_ || now<Time{}) throw std::invalid_argument("Request snapshot clock moved backwards");
    RequestInbox result;
    for (const auto& [_,owner] : owners_) for (const auto& [channel,messages] : owner.messages)
        for (const auto& message : messages) if (message.expires>now) result[channel].push_back(message);
    for (auto& [_,messages] : result) std::sort(messages.begin(),messages.end(),[](const auto& a,const auto& b) {
        return std::tie(a.owner,a.id)<std::tie(b.owner,b.id);
    });
    return result;
}
}
