#include "text.hpp"
#include <cctype>
#include <regex>
#include <sstream>
#include <algorithm>
#include <charconv>

namespace hive::game {
namespace {
std::string words(const std::string& value) {
    std::string out=" ";
    for (unsigned char c : value) {
        if (std::isalnum(c)) out+=static_cast<char>(std::tolower(c));
        else if (out.back()!=' ') out+=' ';
    }
    if (out.back()!=' ') out+=' ';
    return out;
}
std::optional<std::string> station_suffix(std::string line,const TextTarget& target) {
    for (const std::string prefix : {" e "," use "," activate "," the "})
        if (line.starts_with(prefix)) line.erase(0,prefix.size()-1);
    const auto name=words(target.words);
    if (!line.starts_with(name)) return {};
    auto suffix=line.substr(name.size());
    if (target.counted_suffix) {
        const auto end=suffix.find(' ');
        if (end==std::string::npos || !end || suffix.front()=='0') return {};
        unsigned quantity{};
        const auto parsed=std::from_chars(suffix.data(),suffix.data()+end,quantity);
        if (parsed.ec!=std::errc{} || parsed.ptr!=suffix.data()+end || !quantity || quantity>target.counted_suffix->maximum) return {};
        suffix.erase(0,end);
        const auto singular=words(target.counted_suffix->singular),plural=words(target.counted_suffix->plural);
        if (quantity==1 && suffix.starts_with(singular)) suffix.erase(0,singular.size());
        else if (suffix.starts_with(plural)) suffix.erase(0,plural.size());
        else return {};
    }
    if (!std::all_of(suffix.begin(),suffix.end(),[](unsigned char c) { return std::isdigit(c) || c==' '; })) return {};
    return suffix;
}
bool follows(const TextLine& first,const TextLine& second) {
    const auto& a=first.pixels; const auto& b=second.pixels;
    return b.y>=a.y+a.height*.8 && b.y<=a.y+a.height*2.5 && b.height>=a.height*.6 && b.height<=a.height*1.6
        && std::abs((a.x+a.width/2)-(b.x+b.width/2))<=std::max(a.height,b.height)*2;
}
TextLine join(const TextLine& first,const TextLine& second) {
    const auto& a=first.pixels; const auto& b=second.pixels;
    const auto left=std::min(a.x,b.x),right=std::max(a.x+a.width,b.x+b.width);
    return {first.text+" "+second.text,{left,a.y,right-left,b.y+b.height-a.y},
        std::min(first.confidence.value_or(1),second.confidence.value_or(1))};
}
struct Match { Knowledge knowledge; Time remaining{}; };
Match matches(const std::vector<TextLine>& lines,int width,int height,const TextTarget& target) {
    if (width<=0 || height<=0 || lines.size()>2048) return {Knowledge::unknown};
    bool uncertain=false;
    std::optional<Time> duration;
    bool conflict=false;
    const auto accept=[&](Time value) {
        if (duration && *duration!=value) conflict=true;
        else duration=value;
    };
    std::vector<const TextLine*> candidates;
    const auto& region=target.region;
    const Rect bounds{region.x*width,region.y*height,region.width*width,region.height*height};
    static const std::regex timer(R"(\b(?:[0-9]{1,2}:)?[0-5]?[0-9]:[0-5][0-9]\b)");
    for (const auto& line : lines) {
        const auto& box=line.pixels;
        if (!box.valid()) { if (!target.station_name) uncertain=true; continue; }
        if (box.x>=bounds.x+bounds.width || box.y>=bounds.y+bounds.height
            || box.x+box.width<=bounds.x || box.y+box.height<=bounds.y) continue;
        if (line.text.size()>512) { uncertain=true; continue; }
        const auto normalized=words(line.text);
        const auto suffix=target.station_name?station_suffix(normalized,target):std::optional<std::string>{};
        if (target.station_name) candidates.push_back(&line);
        if (box.x<bounds.x || box.y<bounds.y
            || box.x+box.width>bounds.x+bounds.width || box.y+box.height>bounds.y+bounds.height
            || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<0.8f || *line.confidence>1))) {
            if (!target.station_name || suffix) uncertain=true;
            continue;
        }
        std::istringstream required(words(target.words)); std::string word; bool found=true;
        while (required>>word) if (normalized.find(" "+word+" ")==std::string::npos) { found=false; break; }
        if (!found || (target.station_name && !suffix)) continue;
        if (!target.cooldown) {
            if (target.station_name && std::any_of(suffix->begin(),suffix->end(),[](unsigned char c) { return std::isdigit(c); })) continue;
            return {Knowledge::present};
        }
        std::smatch match;
        if (std::regex_search(line.text,match,timer)) {
            if (target.station_name) {
                if (words(*suffix)!=words(match.str())) { uncertain=true; continue; }
            }
            auto digits=match.str(); std::replace(digits.begin(),digits.end(),':',' ');
            std::istringstream parts(digits); int part=0,seconds=0;
            while (parts>>part) seconds=seconds*60+part;
            if (seconds>=86400) { uncertain=true; continue; }
            if (seconds>0) accept(Time(seconds*1000));
        } else if (target.station_name) {
            if (std::any_of(suffix->begin(),suffix->end(),[](unsigned char c) { return std::isdigit(c); })) uncertain=true;
        }
    }
    if (target.station_name && candidates.size()>1) {
        if (candidates.size()>128) return {Knowledge::unknown};
        for (const auto* first : candidates) for (const auto* second : candidates) {
            if (!follows(*first,*second)) continue;
            const auto joined=join(*first,*second);
            const auto result=matches({joined},width,height,target);
            if (result.knowledge==Knowledge::present) {
                if (!target.cooldown) return result;
                accept(result.remaining);
            }
            if (result.knowledge==Knowledge::unknown) uncertain=true;
            const auto suffix=station_suffix(words(joined.text),target);
            if (target.cooldown && suffix && suffix->empty()) for (const auto* third : candidates) {
                if (!follows(*second,*third)) continue;
                const auto timer=matches({join(joined,*third)},width,height,target);
                if (timer.knowledge==Knowledge::present) accept(timer.remaining);
                if (timer.knowledge==Knowledge::unknown) uncertain=true;
            }
        }
    }
    if (conflict) return {Knowledge::unknown};
    if (duration) return {Knowledge::present,*duration};
    return {uncertain?Knowledge::unknown:Knowledge::absent};
}
}
void validate(const TextTarget& target) {
    const auto& r=target.region;
    if (target.words.empty() || target.words.size()>120 || words(target.words)==" " || !r.valid()
        || r.x<0 || r.y<0 || r.x+r.width>1 || r.y+r.height>1) throw std::invalid_argument("Invalid text target");
    if (target.counted_suffix) {
        const auto& suffix=*target.counted_suffix;
        if (!target.station_name || !suffix.maximum || suffix.maximum>100000 || suffix.singular.empty() || suffix.plural.empty()
            || suffix.singular.size()>80 || suffix.plural.size()>80 || words(suffix.singular)==" " || words(suffix.plural)==" "
            || std::any_of(suffix.singular.begin(),suffix.singular.end(),[](unsigned char c) { return !std::isalpha(c) && c!=' '; })
            || std::any_of(suffix.plural.begin(),suffix.plural.end(),[](unsigned char c) { return !std::isalpha(c) && c!=' '; }))
            throw std::invalid_argument("Invalid counted text suffix");
    }
}
bool text_matches(const Frame& frame,const std::vector<TextLine>& lines,const TextTarget& target) {
    validate(target); return frame.valid() && matches(lines,frame.width,frame.height,target).knowledge==Knowledge::present;
}
Reading<bool> read_text_target(const Context& context,const State& state,const TextTarget& target,Time minimum) {
    validate(target);
    Reading<bool> result{Knowledge::unknown,false,state.text.captured,state.text.window,state.text.generation};
    if (!state.text.has_value(context,Time(2000),minimum)) return result;
    result.knowledge=matches(state.text.value.lines,state.text.value.width,state.text.value.height,target).knowledge;
    result.value=result.knowledge==Knowledge::present;
    return result;
}
Reading<Time> read_text_cooldown(const Context& context,const State& state,const TextTarget& target,Time minimum) {
    validate(target);
    if (!target.cooldown || !target.station_name) throw std::invalid_argument("Cooldown requires an exact station target");
    Reading<Time> result{Knowledge::unknown,{},state.text.captured,state.text.window,state.text.generation};
    if (!state.text.has_value(context,Time(2000),minimum)) return result;
    const auto match=matches(state.text.value.lines,state.text.value.width,state.text.value.height,target);
    result.knowledge=match.knowledge; result.value=match.remaining;
    return result;
}
bool text_matches(const Context& context,const State& state,const TextTarget& target,Time minimum) {
    return read_text_target(context,state,target,minimum).knowledge==Knowledge::present;
}
}
