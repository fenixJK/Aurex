#include "join.hpp"
#include <regex>

namespace hive::game {
JoinTarget::JoinTarget(const std::string& link) {
    if (link.empty()) { uri_="roblox://placeId=1537690962"; return; }
    if (link.size()>2048) throw std::invalid_argument("Private server link is too long");
    std::smatch match;
    static const std::regex legacy(R"(^https://www\.roblox\.com/games/1537690962(?:/[A-Za-z0-9_%.-]*)?\?privateServerLinkCode=([A-Za-z0-9_-]{32})$)");
    static const std::regex share(R"(^https://www\.roblox\.com/share\?code=([A-Za-z0-9_-]{32})&type=Server$)");
    if (std::regex_match(link,match,legacy)) uri_="roblox://placeId=1537690962&linkCode="+match[1].str();
    else if (std::regex_match(link,match,share)) uri_="roblox://navigation/share_links?code="+match[1].str()+"&type=Server";
    else throw std::invalid_argument("Enter a Roblox private-server link, or leave it blank for a public server");
}
JoinTarget JoinTarget::from_uri(const std::string& uri) {
    if (uri==JoinTarget{}.uri()) return JoinTarget{};
    static const std::regex allowed(R"(^roblox://(?:placeId=1537690962&linkCode=[A-Za-z0-9_-]{32}|placeId=1537690962&gameInstanceId=[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}|navigation/share_links\?code=[A-Za-z0-9_-]{32}&type=Server|userId=[1-9][0-9]{0,15})$)");
    if (uri.size()>128 || !std::regex_match(uri,allowed)) throw std::invalid_argument("Invalid shared server target");
    JoinTarget result; result.uri_=uri; return result;
}
JoinTarget JoinTarget::follow_user(const std::string& user_id) { return from_uri("roblox://userId="+user_id); }
JoinTarget JoinTarget::public_server(const std::string& instance_id) { return from_uri("roblox://placeId=1537690962&gameInstanceId="+instance_id); }

}
