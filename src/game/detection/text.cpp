#include "text.hpp"
#include <cctype>
#include <regex>
#include <sstream>
#include <algorithm>
#include <charconv>
#include <limits>

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
struct Match { Knowledge knowledge; Time remaining{}; };
Match matches(const std::vector<TextLine>& lines,int width,int height,const TextTarget& target) {
    if (width<=0 || height<=0 || lines.size()>2048) return {Knowledge::unknown};
    bool uncertain=false;
    std::optional<Time> duration;
    bool conflict=false,ready=false,unavailable=false;
    const auto accept=[&](Time value) {
        if (duration && *duration!=value) conflict=true;
        else duration=value;
    };
    const auto& region=target.region;
    const Rect bounds{region.x*width,region.y*height,region.width*width,region.height*height};
    std::vector<TextLine> selected;
    for (const auto& line:lines) {
        const auto& r=line.pixels;
        if (!r.valid()) { if (!target.station_name) uncertain=true; continue; }
        if (r.x>=bounds.x+bounds.width || r.y>=bounds.y+bounds.height || r.x+r.width<=bounds.x || r.y+r.height<=bounds.y) continue;
        if (line.text.size()>512) { uncertain=true; continue; }
        if (r.x<bounds.x || r.y<bounds.y || r.x+r.width>bounds.x+bounds.width || r.y+r.height>bounds.y+bounds.height) {
            if (!target.station_name || station_suffix(words(line.text),target)) uncertain=true;
            continue;
        }
        selected.push_back(line);
    }
    const auto candidates=text_candidates(selected);
    static const std::regex timer(R"(\b(?:[0-9]{1,2}:)?[0-5]?[0-9]:[0-5][0-9]\b)");
    for (const auto& line : candidates) {
        const auto normalized=words(line.text);
        const auto suffix=target.station_name?station_suffix(normalized,target):std::optional<std::string>{};
        const bool has_timer=suffix && std::any_of(suffix->begin(),suffix->end(),[](unsigned char c) { return std::isdigit(c); });
        unavailable|=has_timer;
        if (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<0.8f || *line.confidence>1)) {
            if (!target.station_name || suffix) uncertain=true;
            continue;
        }
        std::istringstream required(words(target.words)); std::string word; bool found=true;
        while (required>>word) if (normalized.find(" "+word+" ")==std::string::npos) { found=false; break; }
        if (!found || (target.station_name && !suffix)) continue;
        if (!target.cooldown) {
            ready|=!has_timer;
            continue;
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
    if (!target.cooldown && ready && (!target.station_name || (!unavailable && !uncertain))) return {Knowledge::present};
    if (conflict) return {Knowledge::unknown};
    if (duration) return {Knowledge::present,*duration};
    return {uncertain?Knowledge::unknown:Knowledge::absent};
}
}
std::vector<TextLine> text_candidates(std::span<const TextLine> lines,unsigned max_rows) {
    std::vector<TextLine> sorted(lines.begin(),lines.end()),rows;
    if (sorted.size()>128 || max_rows<1 || max_rows>4) return sorted;
    std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b) { return a.pixels.x<b.pixels.x; });
    const auto join=[](const TextLine& a,const TextLine& b) {
        const auto x=std::min(a.pixels.x,b.pixels.x),y=std::min(a.pixels.y,b.pixels.y);
        const auto ac=a.confidence.value_or(1),bc=b.confidence.value_or(1);
        const auto confidence=std::isfinite(ac) && std::isfinite(bc) && ac>=0 && ac<=1 && bc>=0 && bc<=1
            ?std::min(ac,bc):std::numeric_limits<float>::quiet_NaN();
        return TextLine{a.text+" "+b.text,{x,y,std::max(a.pixels.x+a.pixels.width,b.pixels.x+b.pixels.width)-x,
            std::max(a.pixels.y+a.pixels.height,b.pixels.y+b.pixels.height)-y},
            confidence};
    };
    for (const auto& line:sorted) {
        if (!line.pixels.valid() || line.text.size()>512) continue;
        auto previous=std::find_if(rows.begin(),rows.end(),[&](const auto& row) {
            const auto a=row.pixels,b=line.pixels;
            return b.x>=a.x+a.width && b.x-a.x-a.width<=std::min(a.height,b.height)*1.5
                && std::abs(a.y+a.height/2-b.y-b.height/2)<=std::min(a.height,b.height)*.4
                && std::min(a.height,b.height)>=std::max(a.height,b.height)*.6 && row.text.size()+line.text.size()<511;
        });
        if (previous==rows.end()) rows.push_back(line);
        else *previous=join(*previous,line);
    }
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b) { return a.pixels.y<b.pixels.y; });
    auto result=rows;
    for (std::size_t i=0;i<rows.size();++i) {
        auto combined=rows[i]; auto last=i;
        for (unsigned n=1;n<max_rows;++n) {
            std::optional<std::size_t> next;
            for (auto j=last+1;j<rows.size();++j) {
                const auto a=rows[last].pixels,b=rows[j].pixels;
                if (b.y>a.y+a.height*2.5) break;
                if (next && b.y>rows[*next].pixels.y+std::min(b.height,rows[*next].pixels.height)*.4) break;
                if (b.y<a.y+a.height*.8 || b.height<a.height*.6 || b.height>a.height*1.6) continue;
                const auto tolerance=std::max(a.height,b.height);
                if (std::abs(a.x+a.width/2-b.x-b.width/2)>tolerance*2
                    && std::abs(a.x-b.x)>tolerance*.75 && std::abs(a.x+a.width-b.x-b.width)>tolerance*.75) continue;
                if (next) { next.reset(); break; }
                next=j;
            }
            if (!next || combined.text.size()+rows[*next].text.size()>511) break;
            combined=join(combined,rows[*next]); result.push_back(combined); last=*next;
        }
    }
    return result;
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
    validate(target); return !target.visual && frame.valid() && matches(lines,frame.width,frame.height,target).knowledge==Knowledge::present;
}
Reading<bool> read_text_target(const Context& context,const State& state,const TextTarget& target,Time minimum) {
    validate(target);
    if (target.visual) {
        const auto& caption=state.interaction_caption;
        Reading<bool> result{Knowledge::unknown,false,caption.captured,caption.window,caption.generation};
        if (!caption.usable(context,Time(2000),minimum)) return result;
        result.value=caption.knowledge==Knowledge::present && caption.value.label==target.words && caption.value.cooldown==target.cooldown;
        result.knowledge=result.value?Knowledge::present:Knowledge::absent;
        return result;
    }
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
    if (target.visual) return result; // Banner color proves cooldown, not its remaining duration.
    if (!state.text.has_value(context,Time(2000),minimum)) return result;
    const auto match=matches(state.text.value.lines,state.text.value.width,state.text.value.height,target);
    result.knowledge=match.knowledge; result.value=match.remaining;
    return result;
}
bool text_matches(const Context& context,const State& state,const TextTarget& target,Time minimum) {
    return read_text_target(context,state,target,minimum).knowledge==Knowledge::present;
}
}
