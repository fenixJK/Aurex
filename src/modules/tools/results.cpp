#include "results.hpp"
#include "game/detection/dialog.hpp"
#include "game/detection/text.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <string_view>

namespace hive::features::tools {
namespace {
std::string words(const std::string& text) {
    std::string result;
    for (std::size_t i=0;i<text.size();++i) {
        const auto ch=static_cast<unsigned char>(text[i]);
        if (ch==0xc3 && i+1<text.size() && static_cast<unsigned char>(text[i+1])==0x97) { result+='x'; ++i; continue; }
        if ((ch<128 && std::isalnum(ch)) || ch=='.' || ch=='%' || ch=='+') result+=char(std::tolower(ch));
        else if (!result.empty() && result.back()!=' ') result+=' ';
    }
    if (!result.empty() && result.back()==' ') result.pop_back();
    return result;
}
bool usable(const TextLine& line,float confidence=.5f) {
    return line.pixels.valid() && line.text.size()<=512 && (!line.confidence || (std::isfinite(*line.confidence) && *line.confidence>=confidence && *line.confidence<=1));
}
bool in(const Rect& line,const Rect& region) {
    return line.x>=region.x && line.y>=region.y && line.x+line.width<=region.x+region.width && line.y+line.height<=region.y+region.height;
}
bool same_label(std::string_view observed,std::string_view expected) {
    if (observed==expected) return true;
    if (expected.size()<6 || std::abs(int(observed.size())-int(expected.size()))>1) return false;
    std::size_t i=0,j=0; bool changed=false;
    while (i<observed.size() && j<expected.size()) {
        if (observed[i]==expected[j]) { ++i; ++j; continue; }
        if (changed) return false;
        changed=true;
        if (observed.size()>=expected.size()) ++i;
        if (expected.size()>=observed.size()) ++j;
    }
    return !changed || (i==observed.size() && j==expected.size());
}
const TextLine* unique(std::span<const TextLine> lines,const std::string& name,const Rect& region) {
    const TextLine* result=nullptr;
    for (const auto& line:lines) if (usable(line,.25f) && in(line.pixels,region) && words(line.text)==name) {
        if (result) return nullptr;
        result=&line;
    }
    return result;
}
bool numeric(const std::string& text,double& value,std::string& suffix,char& unit) {
    static const std::regex pattern(R"(^([+x]?)([0-9]+(?:\.[0-9]+)?)(%?)\s*(.+)$)");
    std::smatch match;
    if (!std::regex_match(text,match,pattern) || match[2].length()>8) return false;
    value=std::stod(match[2]); suffix=match[4]; unit=match[3]=="%"?'%':match[1]=="x"?'x':'+';
    return std::isfinite(value);
}
}
const std::vector<Stat>& amulet_stats() {
    static const std::vector<Stat> values{{"Pollen",5,20},{"White Pollen",15,70},{"Red Pollen",15,70},{"Blue Pollen",15,70},
        {"Bee Gather Pollen",15,70},{"Instant Conversion",3,12},{"Convert Rate",1.05,1.25,true},{"Bee Ability Rate",1,7},{"Critical Chance",1,7}};
    return values;
}
const std::vector<std::string>& passives() {
    static const std::vector<std::string> values{"Pop Star","Scorching Star","Gummy Star","Guiding Star","Star Saw","Star Shower"};
    return values;
}
const std::vector<std::string>& bees() {
    static const std::vector<std::string> values{"Bomber","Brave","Bumble","Cool","Hasty","Looker","Rad","Rascal","Stubborn",
        "Bubble","Bucko","Commander","Demo","Exhausted","Fire","Frosty","Honey","Rage","Riley","Shocked","Baby","Carpenter",
        "Demon","Diamond","Lion","Music","Ninja","Shy","Buoyant","Fuzzy","Precise","Spicy","Tadpole","Vector"};
    return values;
}
void validate(const AmuletFilter& f) {
    const auto valid=[](const std::string& value) { return std::find(passives().begin(),passives().end(),value)!=passives().end(); };
    if (!valid(f.main) || f.minimums.size()>5) throw std::invalid_argument("Choose a main passive and at most five stats");
    for (const auto& side:f.sides) if (!valid(side) || (f.double_passive && side==f.main)) throw std::invalid_argument("Side passives must differ from the main passive");
    for (const auto& [name,value]:f.minimums) {
        const auto stat=std::find_if(amulet_stats().begin(),amulet_stats().end(),[&](const auto& s) { return s.name==name; });
        if (stat==amulet_stats().end() || !std::isfinite(value) || value<stat->minimum || value>stat->maximum)
            throw std::invalid_argument("SSA stat minimum is outside its range");
    }
}
void validate(const BeeFilter& f,bool allow_any) {
    if (!allow_any && f.names.empty() && !f.stop_gifted && !f.stop_mythic) throw std::invalid_argument("Choose a bee or stop condition");
    for (const auto& name:f.names) if (std::find(bees().begin(),bees().end(),name)==bees().end()) throw std::invalid_argument("Unknown bee filter");
}
bool matches(const Amulet& r,const AmuletFilter& f) {
    if (!r.passives.contains(f.main)) return false;
    if (f.double_passive && (r.passives.size()!=2 || (!f.sides.empty() && std::none_of(f.sides.begin(),f.sides.end(),[&](const auto& side) { return side!=f.main && r.passives.contains(side); })))) return false;
    for (const auto& [name,value]:f.minimums) { const auto found=r.stats.find(name); if (found==r.stats.end() || found->second<value) return false; }
    return true;
}
bool matches(const Bee& r,const BeeFilter& f) {
    if ((f.stop_gifted && r.gifted) || (f.stop_mythic && r.mythic)) return true;
    return f.names.contains(r.name);
}
Observation<Amulet> read_amulet(const ObservationContext& c,const game::State& state) {
    const auto& observed=state.text;
    Observation<Amulet> result{Knowledge::unknown,{},observed.captured,observed.window,observed.generation};
    const auto buttons=game::read_dialog(c,observed,{{"Supreme Star Amulet"},{.1,.1,.8,.85},.5f,game::PromptMatch::phrase},{"Keep Old","Replace"});
    if (!buttons.has_value(c,Time(2000))) return result;
    const auto& text=observed.value; const Rect area{text.width*.1,text.height*.1,text.width*.8,text.height*.85};
    const auto labels=game::text_candidates(text.lines,1);
    const auto old=unique(labels,"old",area),fresh=unique(labels,"new",area);
    if (!old || !fresh || old->pixels.x+old->pixels.width>=fresh->pixels.x || std::abs(old->pixels.y-fresh->pixels.y)>fresh->pixels.height) return result;
    const auto divider=(buttons.value.yes.x+buttons.value.no.x)*text.width/2;
    if (fresh->pixels.x<divider || old->pixels.x>divider) return result;
    const double bottom=buttons.value.no.y*text.height;
    Rect stats{divider,fresh->pixels.y+fresh->pixels.height,(buttons.value.no.x-buttons.value.yes.x)*text.width,bottom-fresh->pixels.y-fresh->pixels.height};
    std::vector<TextLine> rows;
    for (const auto& line:text.lines) if (in(line.pixels,stats)) rows.push_back(line);
    bool capacity=false;
    for (const auto& line:game::text_candidates(rows,1)) {
        if (!usable(line)) return result;
        auto value=words(line.text);
        if (value.starts_with("+passive ")) value.erase(0,9);
        else if (value.starts_with("passive ")) value.erase(0,8);
        auto passive=std::find_if(passives().begin(),passives().end(),[&](const auto& name) { return same_label(value,words(name)); });
        if (passive!=passives().end()) { if (!result.value.passives.insert(*passive).second) return result; continue; }
        double amount{}; std::string name; char unit{};
        if (!numeric(value,amount,name,unit)) return result;
        if (name=="capacity") { if (capacity || unit!='x' || std::abs(amount-2.5)>.001) return result; capacity=true; continue; }
        const auto stat=std::find_if(amulet_stats().begin(),amulet_stats().end(),[&](const auto& s) { return same_label(name,words(s.name)); });
        if (stat==amulet_stats().end() || unit!=(stat->multiplier?'x':'%') || amount<stat->minimum || amount>stat->maximum
            || !result.value.stats.emplace(stat->name,amount).second) return result;
    }
    if (capacity && result.value.stats.size()==5 && !result.value.passives.empty() && result.value.passives.size()<=2) {
        result.knowledge=Knowledge::present;
        const auto title=unique(labels,"supreme star amulet",area),button=unique(labels,"keep old",area);
        if (title && button) {
            const double half=(buttons.value.no.x-buttons.value.yes.x)*text.width;
            double top=title->pixels.y-half*.8;
            for (const auto& line:text.lines) if (usable(line,.25f) && words(line.text).starts_with("you created a")
                && line.pixels.x>=divider-half && line.pixels.x+line.pixels.width<=divider+half && line.pixels.y<title->pixels.y)
                top=line.pixels.y-line.pixels.height*.25;
            const double left=std::max(0.,divider-half),right=std::min(double(text.width),divider+half);
            top=std::max(0.,top);
            const double bottom=std::min(double(text.height),button->pixels.y+button->pixels.height*1.25);
            result.value.popup=Rect{left/text.width,top/text.height,(right-left)/text.width,(bottom-top)/text.height};
        }
    }
    return result;
}
Observation<JellyResult> read_jelly(const ObservationContext& c,const game::State& state) {
    const auto& observed=state.text;
    Observation<JellyResult> result{Knowledge::unknown,{},observed.captured,observed.window,observed.generation};
    if (!observed.has_value(c,Time(2000)) || observed.value.width<=0 || observed.value.height<=0
        || observed.value.lines.empty() || observed.value.lines.size()>4096) return result;
    const auto& text=observed.value; const Rect area{text.width*.2,text.height*.15,text.width*.6,text.height*.8};
    const TextLine* button=nullptr; const TextLine* heading=nullptr;
    for (const auto& line:text.lines) if (usable(line) && in(line.pixels,area)) {
        const auto label=words(line.text);
        if (label=="use another royal jelly") { if (button) return result; button=&line; }
        if (label.starts_with("transformed") || label.find(" transformed into")!=std::string::npos || label.starts_with("you hatched")) { if (heading) return result; heading=&line; }
    }
    if (!heading && !button) { result.knowledge=Knowledge::absent; return result; }
    if (!heading || (button && button->pixels.y<=heading->pixels.y)) return result;
    const auto x=button?button->pixels.x+button->pixels.width/2:heading->pixels.x+heading->pixels.width/2;
    const auto bottom=button?button->pixels.y:area.y+area.height;
    const Rect card{std::max(area.x,x-text.width*.23),heading->pixels.y,text.width*.46,bottom-heading->pixels.y};
    for (const auto& line:text.lines) if (usable(line) && in(line.pixels,card)) {
        auto label=words(line.text);
        bool gifted=label.starts_with("gifted "); if (gifted) label.erase(0,7);
        if (label.ends_with(" bee")) label.resize(label.size()-4);
        const auto name=std::find_if(bees().begin(),bees().end(),[&](const auto& bee) { return words(bee)==label; });
        if (name!=bees().end() || label=="basic") {
            if (!result.value.bee.name.empty()) return result;
            result.value.bee.name=name==bees().end()?"Basic":*name; result.value.bee.gifted=gifted;
            result.value.bee.mythic=name!=bees().end() && name>=bees().end()-6;
        }

    }
    if (result.value.bee.name.empty()) return result;
    if (button) result.value.again=Point{x/text.width,(button->pixels.y+button->pixels.height/2)/text.height};
    result.knowledge=Knowledge::present; return result;
}
}
