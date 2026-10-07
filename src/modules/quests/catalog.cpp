#include "hive/modules/quests/catalog.hpp"
#include "catalog_data.hpp"
#include <algorithm>
#include <cctype>
#include <numeric>
#include <regex>
#include <array>
#include <set>

namespace hive::features::quests {
namespace {
std::string key(const std::string& text) {
    std::string value;
    for (unsigned char c:text) if (std::isalnum(c)) value+=char(std::tolower(c));
    return value;
}
unsigned distance(const std::string& a,const std::string& b) {
    std::vector<unsigned> old(b.size()+1),next(b.size()+1); std::iota(old.begin(),old.end(),0);
    for (std::size_t i=0;i<a.size();++i) {
        next[0]=unsigned(i+1);
        for (std::size_t j=0;j<b.size();++j) next[j+1]=std::min({old[j+1]+1,next[j]+1,old[j]+unsigned(a[i]!=b[j])});
        old.swap(next);
    }
    return old.back();
}
}
std::span<const QuestDefinition> quest_catalog() { return quest_definitions; }
const QuestDefinition* identify_quest(const std::string& title,const std::string& giver) {
    if (title.empty() || title.size()>160) return {};
    static const std::regex possessive(R"((black bear|polar bear|bucko bee|riley bee|honey bee)(?:'|’)s )",std::regex::icase);
    auto input=key(std::regex_replace(title,possessive,"$1: ")),owner=key(giver);
    for (const auto* name:{"Black Bear","Polar Bear","Bucko Bee","Riley Bee","Honey Bee"}) {
        for (const auto prefix:{key(name),"gifted"+key(name)}) if (input.starts_with(prefix)) {
            if (!owner.empty() && owner!=key(name)) return {};
            owner=key(name); input.erase(0,prefix.size()); break;
        }
    }
    if (input.empty()) return {};
    unsigned best=1000,next=1000; const QuestDefinition* match=nullptr;
    for (const auto& quest:quest_definitions) {
        if (!owner.empty() && key(quest.giver)!=owner) continue;
        const auto name=key(quest.name);
        const unsigned allowed=name.size()>=16?2:name.size()>=8?1:0;
        if (std::abs(int(input.size())-int(name.size()))>int(allowed)) continue;
        const auto score=distance(input,name);
        if (score>allowed) continue;
        if (score<best) { next=best; best=score; match=&quest; }
        else next=std::min(next,score);
    }
    return match && next>best?match:nullptr;
}
std::optional<QuestDefinition> resolve_quest(const std::string& title,const std::string& giver) {
    auto input=key(title);
    if (title.size()>160) return {};
    const bool prefix=input.starts_with("brownbear");
    if (!prefix && key(giver)!="brownbear") {
        if (const auto* quest=identify_quest(title,giver)) return *quest;
        return {};
    }
    if (!giver.empty() && key(giver)!="brownbear") return {};
    if (prefix) input.erase(0,9);
    const bool solo=input.starts_with("solo");
    if (solo) input.erase(0,4);
    static constexpr std::array codes{
        std::pair{"Sunf","Sunflower"},std::pair{"Sun","Sunflower"},std::pair{"Dand","Dandelion"},
        std::pair{"Mush","Mushroom"},std::pair{"Bluf","Blue Flower"},std::pair{"Clove","Clover"},std::pair{"Clov","Clover"},
        std::pair{"Bamb","Bamboo"},std::pair{"Spide","Spider"},std::pair{"Straw","Strawberry"},
        std::pair{"Pinap","Pineapple"},std::pair{"Stump","Stump"},std::pair{"Cact","Cactus"},
        std::pair{"Pump","Pumpkin"},std::pair{"Pine","Pine Tree"},std::pair{"Rose","Rose"},
        std::pair{"Mount","Mountain Top"},std::pair{"Coco","Coconut"},std::pair{"Pepp","Pepper"},
        std::pair{"Red","Red"},std::pair{"Blue","Blue"},std::pair{"White","White"}};
    QuestDefinition quest{"Brown Bear",solo?"Solo":"",{}};
    std::set<std::string> targets;
    bool color=false;
    while (!input.empty() && quest.rows.size()<4) {
        const auto found=std::find_if(codes.begin(),codes.end(),[&](const auto& code) { return input.starts_with(key(code.first)); });
        if (found==codes.end() || !targets.insert(found->second).second) return {};
        const bool is_color=std::string_view(found->second)=="Red" || std::string_view(found->second)=="Blue" || std::string_view(found->second)=="White";
        if (is_color && (!quest.rows.empty() || solo)) return {};
        color|=is_color;
        if (!quest.name.empty()) quest.name+='-';
        quest.name+=std::string_view(found->first)=="Sunf"?"Sun":std::string_view(found->first)=="Clov"?"Clove":found->first;
        quest.rows.push_back({"collect",found->second,{}});
        input.erase(0,std::char_traits<char>::length(found->first));
    }
    const auto count=quest.rows.size();
    if (!input.empty() || (solo?count!=1:color?(count!=2 && count!=3):(count!=2 && count!=4))) return {};
    return quest;
}
Objective catalog_objective(const QuestDefinition& quest,std::size_t row,std::string id,bool complete) {
    if (row>=quest.rows.size()) throw std::invalid_argument("Quest row is outside its definition");
    const auto& requirement=quest.rows[row];
    Objective result{std::move(id),requirement.action+" "+requirement.target};
    result.required=1; result.collected=complete?1:0; result.known=true; result.visual=true;
    const auto target=key(requirement.target);
    if (requirement.action=="collect" || requirement.action=="goo") {
        result.goo=requirement.action=="goo";
        if (target=="red") result.kind=Kind::red_pollen;
        else if (target=="blue") result.kind=Kind::blue_pollen;
        else if (target=="white") result.kind=Kind::white_pollen;
        else if (target=="any" || target=="honeytokens") result.kind=Kind::gather_activity;
        else { result.kind=Kind::field_pollen; result.field=requirement.target; }
    } else if (requirement.action=="kill") {
        result.kind=Kind::enemy;
        result.enemy=target=="ladybugs"?"Ladybug":target=="rhinobeetles"?"Rhino Beetle":target=="scorpions"?"Scorpion":requirement.target;
        if (target=="all") result.kind=Kind::any_enemy;
    } else if (requirement.action=="petals") {
        result.kind=Kind::petals; result.target=requirement.target; result.text=requirement.target;
        result.field=requirement.field; result.petal_color=requirement.color;
    } else if (requirement.action=="track") {
        result.kind=Kind::tracked; result.target=requirement.target; result.text=requirement.target;
    } else {
        result.kind=requirement.action=="feed"?Kind::feeding:target=="ant"?Kind::challenge:Kind::station;
        result.target=requirement.target;
    }
    return result;
}
}
