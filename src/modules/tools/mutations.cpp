#include "hive/modules/tools/mutations.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace hive::features::tools {
namespace {
std::string compact(std::string value) {
    std::string out;
    for (unsigned char c:value) if ((c<128 && std::isalnum(c)) || c=='+' || c=='%' || c=='.' || c==',') out+=char(std::tolower(c));
    return out;
}
bool usable(const TextLine& line) {
    return line.pixels.valid() && line.text.size()<=512 && (!line.confidence || (std::isfinite(*line.confidence) && *line.confidence>=.5f && *line.confidence<=1));
}
}
const std::vector<MutationType>& mutation_types() {
    static const std::vector<MutationType> types{{"Ability Rate",false,true},{"Gather Amount",true,true},{"Convert Amount",true,true},
        {"Instant Conversion",false,true},{"Critical Chance",false,true},{"Attack",true,true},{"Energy",false,true},{"Movespeed",true,false}};
    return types;
}
void validate(const MutationFilter& filter) {
    std::set<std::string> names;
    for (const auto& rule:filter) {
        const auto type=std::find_if(mutation_types().begin(),mutation_types().end(),[&](const auto& t) {return t.name==rule.name;});
        if (type==mutation_types().end() || !names.insert(rule.name).second || !std::isfinite(rule.minimum) || rule.minimum<0
            || rule.minimum>1000000 || (rule.minimum>0 && !(rule.percent?type->percent:type->flat))) throw std::invalid_argument("Invalid mutation filter");
    }
}
bool matches(const Mutation& mutation,const MutationFilter& filter) {
    if (mutation.name.empty()) return false;
    return filter.empty() || std::any_of(filter.begin(),filter.end(),[&](const auto& rule) {
        return rule.name==mutation.name && (rule.minimum==0 || (rule.percent==mutation.percent && mutation.value>=rule.minimum));
    });
}
std::string describe(const Mutation& mutation) {
    std::ostringstream out; out<<'+'<<mutation.value<<(mutation.percent?"% ":" ")<<mutation.name; return out.str();
}
Observation<MutationPanel> read_mutation(const ObservationContext& c,const game::State& state) {
    const auto& observed=state.text;
    Observation<MutationPanel> out{Knowledge::unknown,{},observed.captured,observed.window,observed.generation};
    if (!observed.has_value(c,Time(2000)) || observed.value.width<=0 || observed.value.height<=0
        || observed.value.lines.empty() || observed.value.lines.size()>4096) return out;
    const auto& text=observed.value;
    const TextLine* heading=nullptr; const TextLine* bee=nullptr; bool uncertain=false;
    for (const auto& line:text.lines) {
        const auto r=line.pixels;
        if (r.x<text.width*.02 || r.x+r.width>text.width*.95 || r.y<text.height*.12 || r.y>text.height*.85) continue;
        const auto label=compact(line.text);
        if (label=="mutation") {
            if (heading || !usable(line)) return out;
            heading=&line;
        } else if (label.find("mutation")!=std::string::npos || label.starts_with("mutat")) uncertain=true;
        if (label.find("beelvl")!=std::string::npos && usable(line)) {
            if (bee) return out; bee=&line;
        }
    }
    if (bee) {
        const auto r=bee->pixels;
        const bool combined=compact(bee->text).starts_with("x");
        Point close{(combined?r.x+r.height*.5:r.x-r.height*.85)/text.width,(r.y+r.height*.5)/text.height};
        for (const auto& line:text.lines) if (usable(line) && compact(line.text)=="x" && line.pixels.x<r.x
            && r.x-line.pixels.x<r.height*3 && std::abs(line.pixels.y-r.y)<r.height) {
            close={(line.pixels.x+line.pixels.width/2)/text.width,(line.pixels.y+line.pixels.height/2)/text.height}; break;
        }
        if (close.x>0 && close.x<1 && close.y>0 && close.y<1) { out.value.close=close; out.value.bee_menu=true; }
    }
    if (!heading) { if (!uncertain) out.knowledge=Knowledge::absent; return out; }
    const auto h=heading->pixels;
    std::vector<const TextLine*> rows;
    for (const auto& line:text.lines) {
        const auto r=line.pixels;
        if (&line==heading || r.x<h.x-h.width*.3 || r.x+r.width>h.x+h.width*1.3 || r.y<h.y+h.height*.65 || r.y+r.height>h.y+h.height*6) continue;
        if (!usable(line)) return out;
        rows.push_back(&line);
    }
    std::sort(rows.begin(),rows.end(),[](auto a,auto b) {return a->pixels.y<b->pixels.y || (a->pixels.y==b->pixels.y && a->pixels.x<b->pixels.x);});
    std::string value; for (const auto line:rows) value+=compact(line->text);
    if (value.starts_with("+")) value.erase(0,1);
    static const std::regex amount_first(R"(^([0-9]{1,6}(?:[.,][0-9]{1,2})?)(%?)([a-z]+)$)");
    static const std::regex name_first(R"(^([a-z]+)\+?([0-9]{1,6}(?:[.,][0-9]{1,2})?)(%?)$)");
    std::smatch match; std::string amount,name,unit;
    if (std::regex_match(value,match,amount_first)) { amount=match[1]; unit=match[2]; name=match[3]; }
    else if (std::regex_match(value,match,name_first)) {name=match[1];amount=match[2];unit=match[3];}
    else return out;
    if (name=="beeabilityrate") name="abilityrate";
    if (name=="critchance") name="criticalchance";
    const auto type=std::find_if(mutation_types().begin(),mutation_types().end(),[&](const auto& t) {return compact(t.name)==name;});
    const bool percent=unit=="%";
    if (type==mutation_types().end() || !(percent?type->percent:type->flat)) return out;
    std::replace(amount.begin(),amount.end(),',','.'); const double number=std::stod(amount);
    if (number<=0) return out;
    out.value.mutation={type->name,number,percent}; out.knowledge=Knowledge::present; return out;
}
}
