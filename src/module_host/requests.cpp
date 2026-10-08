#include "hive/module_host/requests.hpp"
#include <algorithm>
#include <tuple>
#include "hive/game/data/fields.hpp"

namespace hive::game {
namespace {
constexpr std::array<std::string_view,3> menu_names{"inventory","quests","settings"};
MenuTab request_menu(const SettingValue& value) {
    if (const auto* name=std::get_if<std::string>(&value))
        for (std::size_t i=0;i<menu_names.size();++i) if (*name==menu_names[i]) return MenuTab(i);
    throw std::invalid_argument("Invalid field tracking menu");
}
}
RequestContracts game_request_contracts() {
    return {
        {"servers",[](const Settings& data) {
            const auto target=data.find("target"),epoch=data.find("epoch"),interrupt=data.find("interrupt");
            if (data.size()!=2+unsigned(interrupt!=data.end()) || target==data.end() || epoch==data.end()
                || !std::holds_alternative<std::string>(target->second) || !std::holds_alternative<std::string>(epoch->second)
                || !request_identifier(std::get<std::string>(epoch->second))
                || (interrupt!=data.end() && !std::holds_alternative<bool>(interrupt->second))) throw std::invalid_argument("Invalid server request");
            JoinTarget::from_uri(std::get<std::string>(target->second));
        }},
        {"fields",[](const Settings& data) {
            const auto field=data.find("field"),alternatives=data.find("alternatives"),complete=data.find("complete"),slot=data.find("hotbar-slot"),interrupt=data.find("interrupt");
            const auto menu=data.find("menu");
            if (field==data.end() || !std::holds_alternative<std::string>(field->second)
                || data.size()!=1+unsigned(alternatives!=data.end())+unsigned(complete!=data.end())+unsigned(slot!=data.end())+unsigned(interrupt!=data.end())+unsigned(menu!=data.end())
                || (interrupt!=data.end() && !std::holds_alternative<bool>(interrupt->second))
                || (slot!=data.end() && (!std::holds_alternative<int>(slot->second) || std::get<int>(slot->second)<0 || std::get<int>(slot->second)>7))
                || (complete!=data.end() && !std::holds_alternative<bool>(complete->second))
                || (alternatives!=data.end() && !std::holds_alternative<SettingSelection>(alternatives->second)))
                throw std::invalid_argument("Invalid field request data");
            validate_field_targets(std::get<std::string>(field->second),alternatives==data.end()?SettingSelection{}:std::get<SettingSelection>(alternatives->second));
            if (menu!=data.end()) request_menu(menu->second);
        }},
        {"enemies",[](const Settings& data) {
            const auto enemy=data.find("enemy"),field=data.find("field");
            if (data.size()!=1+unsigned(field!=data.end()) || enemy==data.end() || !std::holds_alternative<std::string>(enemy->second)
                || !request_identifier(std::get<std::string>(enemy->second))) throw std::invalid_argument("Invalid enemy request data");
            if (field!=data.end() && (!std::holds_alternative<std::string>(field->second)
                || !field_at(std::get<std::string>(field->second)+".entry"))) throw std::invalid_argument("Invalid enemy field");
        }},
        {"stations",[](const Settings& data) {
            const auto station=data.find("station");
            if (data.size()!=1 || station==data.end() || !std::holds_alternative<std::string>(station->second)
                || !request_identifier(std::get<std::string>(station->second))) throw std::invalid_argument("Invalid station request data");
        }}};
}
std::optional<ServerGoal> server_goal(const RequestInbox& inbox,Time now,std::uint64_t generation) {
    const auto found=inbox.find("servers"); if (found==inbox.end()) return {};
    const RequestMessage* best=nullptr;
    for (const auto& request:found->second) if (request.generation==generation && request.expires>now
        && (!best || request.priority>best->priority || (request.priority==best->priority
            && std::tie(request.owner,request.id)<std::tie(best->owner,best->id)))) best=&request;
    if (!best) return {};
    auto target=JoinTarget::from_uri(std::get<std::string>(best->data.at("target")));
    auto key=best->owner+"/"+best->id+"/"+std::get<std::string>(best->data.at("epoch"))+"/"+target.uri();
    return ServerGoal{std::move(target),std::move(key),best->expires,!best->data.contains("interrupt") || std::get<bool>(best->data.at("interrupt"))};
}
RequestBatch channel_batch(const Requests& requests) {
    RequestBatch result{requests.generation,requests.revision,requests.channels};
    if (!requests.fields.empty()) {
        if (result.channels.contains("fields")) throw std::invalid_argument("Duplicate field request representations");
        for (const auto& r : requests.fields) {
            Settings data{{"field",r.field},{"alternatives",r.alternatives},{"complete",r.complete},{"hotbar-slot",int(r.hotbar_slot)},{"interrupt",r.interrupt}};
            if (r.menu) data["menu"]=std::string(menu_names.at(unsigned(*r.menu)));
            result.channels["fields"].push_back({r.id,r.priority,r.expires,std::move(data)});
        }
    }
    if (!requests.enemies.empty()) {
        if (result.channels.contains("enemies")) throw std::invalid_argument("Duplicate enemy request representations");
        for (const auto& r : requests.enemies) {
            Settings data{{"enemy",r.enemy}}; if (!r.field.empty()) data["field"]=r.field;
            result.channels["enemies"].push_back({r.id,r.priority,r.expires,std::move(data)});
        }
    }
    return result;
}
RequestScope RequestBroker::grant(std::string owner,RequestLimits limits,std::optional<RequestLimits> enemies) {
    RequestGrants grants{{"fields",limits}};
    if (enemies) grants.emplace("enemies",*enemies);
    return grant_channels(std::move(owner),std::move(grants));
}
void RequestBroker::revoke(RequestScope scope) noexcept { channels_.revoke(scope); }
bool RequestBroker::replace(RequestScope scope,const Requests& requests,Time now) { return replace_channels(scope,channel_batch(requests),now); }
std::vector<FieldGoal> RequestBroker::snapshot(Time now,std::uint64_t generation) const {
    const auto all=channels(now,generation); const auto found=all.find("fields");
    std::vector<FieldGoal> result;
    if (found!=all.end()) for (const auto& m : found->second) {
        const auto alternatives=m.data.find("alternatives");
        result.push_back({m.owner,m.id,std::get<std::string>(m.data.at("field")),m.priority,m.expires,m.generation,
            alternatives==m.data.end()?SettingSelection{}:std::get<SettingSelection>(alternatives->second),
            m.data.contains("complete") && std::get<bool>(m.data.at("complete")),
            m.data.contains("hotbar-slot")?unsigned(std::get<int>(m.data.at("hotbar-slot"))):0u,
            m.data.contains("interrupt") && std::get<bool>(m.data.at("interrupt")),
            m.data.contains("menu")?std::optional(request_menu(m.data.at("menu"))):std::nullopt});
    }
    return result;
}
std::vector<EnemyGoal> RequestBroker::enemies(Time now,std::uint64_t generation) const {
    const auto all=channels(now,generation); const auto found=all.find("enemies");
    std::vector<EnemyGoal> result;
    if (found!=all.end()) for (const auto& m : found->second)
        result.push_back({m.owner,m.id,std::get<std::string>(m.data.at("enemy")),m.priority,m.expires,m.generation,m.data.contains("field")?std::get<std::string>(m.data.at("field")):std::string{}});
    return result;
}
void SharedRequests::configure(RequestContracts contracts) { std::lock_guard lock(mutex_); broker_.configure(std::move(contracts)); }
void SharedRequests::reset(std::uint64_t generation) { std::lock_guard lock(mutex_); broker_.reset(generation); published_={}; }
std::uint64_t SharedRequests::generation() const { std::lock_guard lock(mutex_); return broker_.generation(); }
RequestScope SharedRequests::grant(std::string owner,RequestLimits limits,std::optional<RequestLimits> enemies) { std::lock_guard lock(mutex_); return broker_.grant(std::move(owner),limits,enemies); }
RequestScope SharedRequests::grant_channels(std::string owner,RequestGrants grants) { std::lock_guard lock(mutex_); return broker_.grant_channels(std::move(owner),std::move(grants)); }
void SharedRequests::revoke(RequestScope scope) noexcept { std::lock_guard lock(mutex_); broker_.revoke(scope); }
bool SharedRequests::replace(RequestScope scope,const Requests& requests,Time now) { return replace_channels(scope,channel_batch(requests),now); }
bool SharedRequests::replace_channels(RequestScope scope,const RequestBatch& batch,Time now) {
    std::lock_guard lock(mutex_); const auto replaced=broker_.replace_channels(scope,batch,now);
    if (replaced) published_=now;
    return replaced;
}
RequestInbox SharedRequests::channels(Time now,std::uint64_t generation) const {
    if (now<Time{}) throw std::invalid_argument("Invalid request snapshot time");
    std::lock_guard lock(mutex_); return broker_.channels(std::max(now,published_),generation);
}
std::vector<FieldGoal> SharedRequests::snapshot(Time now,std::uint64_t generation) const {
    if (now<Time{}) throw std::invalid_argument("Invalid request snapshot time");
    std::lock_guard lock(mutex_); return broker_.snapshot(std::max(now,published_),generation);
}
std::vector<EnemyGoal> SharedRequests::enemies(Time now,std::uint64_t generation) const {
    if (now<Time{}) throw std::invalid_argument("Invalid request snapshot time");
    std::lock_guard lock(mutex_); return broker_.enemies(std::max(now,published_),generation);
}
}
