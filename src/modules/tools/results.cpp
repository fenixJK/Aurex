#include "results.hpp"
#include "game/detection/dialog.hpp"
#include "game/detection/visual_labels.hpp"
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
const TextLine* unique(std::span<const TextLine> lines,const std::string& name,const Rect& region) {
    const TextLine* result=nullptr;
    for (const auto& line:lines) if (usable(line,.25f) && in(line.pixels,region) && words(line.text)==name) {
        if (result) return nullptr;
        result=&line;
    }
    return result;
}
bool numeric(const std::string& text,double& value,char& unit,std::string& suffix) {
    static const std::regex pattern(R"(^([+x]?)\s*([0-9]+(?:\s*\.\s*[0-9]+)?)\s*(%?)\s*(.*)$)");
    std::smatch match;
    if (!std::regex_match(text,match,pattern)) return false;
    auto number=match[2].str();
    std::erase_if(number,[](unsigned char ch) { return std::isspace(ch); });
    if (number.size()>8) return false;
    suffix=match[4]; value=std::stod(number); unit=match[3]=="%"?'%':match[1]=="x"?'x':'+';
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
    const auto& observed=state.fixed_labels;
    Observation<Amulet> result{Knowledge::unknown,{},observed.captured,observed.window,observed.generation};
    const auto buttons=game::read_dialog(c,observed,{{"Supreme Star Amulet"},{.1,.1,.8,.85},.5f,game::PromptMatch::phrase},{"Keep Old","Replace"});
    if (!buttons.has_value(c,Time(2000)) || !state.text.has_value(c,Time(2000))
        || state.text.value.width!=observed.value.width || state.text.value.height!=observed.value.height) return result;
    result.captured=std::min(result.captured,state.text.captured);
    const auto& text=observed.value; const Rect area{text.width*.1,text.height*.1,text.width*.8,text.height*.85};
    const auto labels=game::text_candidates(text.lines,1);
    const auto divider=(buttons.value.yes.x+buttons.value.no.x)*text.width/2;
    const double bottom=buttons.value.no.y*text.height;
    // The generator caption can remain above the result. Associate the title
    // nearest the result controls rather than requiring it to be unique on screen.
    const TextLine* title=nullptr;
    for (const auto& line:labels) if (usable(line) && in(line.pixels,area)
        && words(line.text)=="supreme star amulet" && line.pixels.y+line.pixels.height<bottom
        && (!title || line.pixels.y>title->pixels.y)) title=&line;
    if (!title) return result;
    const double top=title->pixels.y+title->pixels.height*2;
    Rect stats{divider,top,(buttons.value.no.x-buttons.value.yes.x)*text.width,bottom-top};
    std::vector<TextLine> rows;
    for (const auto& line:text.lines) if (in(line.pixels,stats)) rows.push_back(line);
    bool capacity=false;
    for (const auto& line:game::text_candidates(rows,1)) {
        if (!line.text.empty() && !usable(line)) return result;
        auto value=words(line.text);
        std::string_view passive_label=value;
        if (passive_label.starts_with("+")) {
            passive_label.remove_prefix(1);
            while (passive_label.starts_with(" ")) passive_label.remove_prefix(1);
        }
        if (passive_label.starts_with("passive ")) passive_label.remove_prefix(8);
        auto passive=std::find_if(passives().begin(),passives().end(),[&](const auto& name) { return passive_label==words(name); });
        if (passive!=passives().end()) { if (!result.value.passives.insert(*passive).second) return result; continue; }
        double amount{}; char unit{}; bool read=false;
        // Visual labels identify the row. Its existing OCR crop supplies the value,
        // and can identify a tiny label that image matching could not resolve.
        for (const auto& candidate:state.text.value.lines) {
            const auto a=line.pixels,b=candidate.pixels;
            if (!usable(candidate) || b.x<a.x-a.height || b.x>a.x+a.height
                || std::abs(b.y+b.height/2-a.y-a.height/2)>std::max(a.height,b.height)*.5) continue;
            double next{}; char next_unit{}; std::string suffix;
            if (!numeric(words(candidate.text),next,next_unit,suffix)) continue;
            if (line.text.empty() && read && value!=suffix) return result;
            if (value.empty()) value=std::move(suffix);
            if (read && (next!=amount || next_unit!=unit)) return result;
            amount=next;unit=next_unit;read=true;
        }
        if (!read) return result;
        const auto& name=value;
        if (name=="capacity") { if (capacity || unit!='x' || std::abs(amount-2.5)>.001) return result; capacity=true; continue; }
        const auto stat=std::find_if(amulet_stats().begin(),amulet_stats().end(),[&](const auto& s) { return name==words(s.name); });
        if (stat==amulet_stats().end() || unit!=(stat->multiplier?'x':'%') || amount<stat->minimum || amount>stat->maximum
            || !result.value.stats.emplace(stat->name,amount).second) return result;
    }
    if (capacity && result.value.stats.size()==5 && !result.value.passives.empty() && result.value.passives.size()<=2) {
        result.knowledge=Knowledge::present;
        const auto button=unique(labels,"keep old",area);
        if (button) {
            const double half=(buttons.value.no.x-buttons.value.yes.x)*text.width;
            double top=title->pixels.y-half*.8;
            const double left=std::max(0.,divider-half),right=std::min(double(text.width),divider+half);
            top=std::max(0.,top);
            const double bottom=std::min(double(text.height),button->pixels.y+button->pixels.height*1.25);
            result.value.popup=Rect{left/text.width,top/text.height,(right-left)/text.width,(bottom-top)/text.height};
        }
    }
    return result;
}
Observation<JellyResult> read_jelly(const ObservationContext& c,const game::State& state) {
    const auto& observed=state.fixed_labels;
    Observation<JellyResult> result{Knowledge::unknown,{},observed.captured,observed.window,observed.generation};
    if (!observed.has_value(c,Time(2000)) || observed.value.width<=0 || observed.value.height<=0
        || observed.value.lines.size()>4096) return result;
    const auto& text=observed.value; const Rect area{text.width*.2,text.height*.15,text.width*.6,text.height*.8};
    const auto* button=unique(text.lines,"use another",area);
    if (!button) { result.knowledge=Knowledge::absent; return result; }
    const auto x=button->pixels.x+button->pixels.width/2;
    const auto bottom=button->pixels.y;
    const auto top=std::max(area.y,bottom-button->pixels.width*3);
    const Rect card{std::max(area.x,x-text.width*.23),top,text.width*.46,bottom-top};
    for (const auto& line:text.lines) if (usable(line) && in(line.pixels,card)) {
        auto label=words(line.text);
        bool gifted=label.starts_with("gifted "); if (gifted) label.erase(0,7);
        if (const auto suffix=label.find(" bee");suffix!=std::string::npos) label.resize(suffix);
        const auto name=std::find_if(bees().begin(),bees().end(),[&](const auto& bee) { return words(bee)==label; });
        if (name!=bees().end() || label=="basic") {
            if (!result.value.bee.name.empty()) return result;
            result.value.bee.name=name==bees().end()?"Basic":*name; result.value.bee.gifted=gifted;
            result.value.bee.mythic=name!=bees().end() && name>=bees().end()-6;
        }
    }
    if (result.value.bee.name.empty()) return result;
    result.value.again=Point{x/text.width,(button->pixels.y+button->pixels.height/2)/text.height};
    result.knowledge=Knowledge::present; return result;
}
std::vector<Rect> amulet_number_regions(const ObservationContext& c,const game::State& state) {
    const auto& labels=state.fixed_labels;
    if (!labels.has_value(c,Time(2000))) return {};
    std::optional<Rect> bounds;
    for (const auto& line:labels.value.lines) {
        if (!line.text.empty() && line.text!="Capacity" && std::none_of(amulet_stats().begin(),amulet_stats().end(),[&](const auto& stat){return stat.name==line.text;})) continue;
        const auto b=line.pixels;
        if (!bounds) bounds=b;
        else {const auto a=*bounds;bounds=Rect{std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x+a.width,b.x+b.width)-std::min(a.x,b.x),std::max(a.y+a.height,b.y+b.height)-std::min(a.y,b.y)};}
    }
    if (!bounds) return {};
    const auto b=*bounds;const double pad=std::max(4.,b.width*.025);
    const double left=std::max(0.,b.x-pad),top=std::max(0.,b.y-pad);
    const double right=std::min(double(labels.value.width),b.x+b.width+pad),bottom=std::min(double(labels.value.height),b.y+b.height+pad);
    return {{left/labels.value.width,top/labels.value.height,(right-left)/labels.value.width,(bottom-top)/labels.value.height}};
}
std::vector<game::FixedLabelQuery> amulet_labels() {
    game::FixedLabelQuery query{{-.3,-1.3,1.5,1.3},{"Capacity"},game::LabelInk::dark,"Replace",true};
    for (const auto& stat:amulet_stats()) query.labels.push_back(stat.name);
    for (const auto& passive:passives()) query.labels.push_back("+Passive: "+passive);
    return {{{.1,.1,.8,.85},{"Keep Old","Replace"},game::LabelInk::light},
        {{.1,.1,.8,.85},{"Supreme Star Amulet"},game::LabelInk::dark},std::move(query)};
}
std::vector<game::FixedLabelQuery> bee_labels() {
    game::FixedLabelQuery query{{-.75,-2.59,2.1,.32},{"Basic Bee!","Gifted Basic Bee!"},game::LabelInk::light,"Use Another",true};
    for (const auto& bee:bees()) {
        query.labels.push_back(bee+" Bee!");query.labels.push_back("Gifted "+bee+" Bee!");
    }
    return {{{.2,.15,.6,.8},{"Use Another"},game::LabelInk::dark},std::move(query)};
}
}
