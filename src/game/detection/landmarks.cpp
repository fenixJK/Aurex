#include "landmarks.hpp"
#include "text.hpp"
#include <algorithm>
#include <cctype>

namespace hive::game {
namespace {
std::string normalize(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isspace(c)) { if (!out.empty() && out.back()!=' ') out+=' '; }
        else out+=static_cast<char>(std::tolower(c));
    }
    if (!out.empty() && out.back()==' ') out.pop_back();
    return out;
}
std::string words(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c)) out+=static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back()!=' ') out+=' ';
    }
    if (!out.empty() && out.back()==' ') out.pop_back();
    return out;
}
bool matches(const std::string& text,const std::string& phrase,PromptMatch mode) {
    if (mode==PromptMatch::exact) return words(text)==words(phrase);
    if (mode==PromptMatch::prefix) return (words(text)+" ").starts_with(words(phrase)+" ");
    return (" "+words(text)+" ").find(" "+words(phrase)+" ")!=std::string::npos;
}
}
void validate(const PromptLandmark& p) {
    const auto& r=p.region;
    if ((p.match!=PromptMatch::exact && p.match!=PromptMatch::phrase && p.match!=PromptMatch::prefix) || p.phrases.empty() || p.phrases.size()>16 || !r.valid() || r.x<0 || r.y<0
        || r.x+r.width>1 || r.y+r.height>1 || !std::isfinite(p.confidence) || p.confidence<0 || p.confidence>1)
        throw std::invalid_argument("Invalid prompt landmark");
    for (const auto& phrase : p.phrases)
        if (phrase.size()>120 || words(phrase).empty()) throw std::invalid_argument("Invalid landmark phrase");
}
Reading<bool> read_prompt_landmark(const Context& c,const Reading<TextSnapshot>& text,const PromptLandmark& p) {
    validate(p);
    Reading<bool> result{Knowledge::unknown,false,text.captured,text.window,text.generation};
    if (!text.has_value(c,Time(2000)) || text.value.width<=0 || text.value.height<=0) return result;
    const auto& snapshot=text.value;
    const Rect bounds{p.region.x*snapshot.width,p.region.y*snapshot.height,
        p.region.width*snapshot.width,p.region.height*snapshot.height};
    if (snapshot.lines.size()>4096) return result;
    bool uncertain=false;
    std::vector<TextLine> candidates;
    for (const auto& line : snapshot.lines) {
        const auto& r=line.pixels;
        if (!r.valid() || r.x<bounds.x || r.y<bounds.y || r.x+r.width>bounds.x+bounds.width
            || r.y+r.height>bounds.y+bounds.height || line.text.size()>512) continue;
        if (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<p.confidence || *line.confidence>1)) {
            if (!p.ignore_unrelated_uncertainty) { uncertain=true; continue; }
            const auto observed=" "+words(line.text)+" ";
            for (const auto& phrase:p.phrases) {
                const auto target=words(phrase);
                for (std::size_t begin=0;begin<target.size();) {
                    const auto end=target.find(' ',begin);
                    const auto word=target.substr(begin,end==std::string::npos?end:end-begin);
                    if (word.size()>=4 && observed.find(" "+word+" ")!=std::string::npos) uncertain=true;
                    if (end==std::string::npos) break;
                    begin=end+1;
                }
            }
            continue;
        }
        candidates.push_back(line);
    }
    for (const auto& line:text_candidates(candidates)) {
        auto normalized=normalize(line.text);
        if (normalized.starts_with("e ")) normalized.erase(0,2);
        if (std::any_of(p.phrases.begin(),p.phrases.end(),[&](const auto& phrase) { return matches(normalized,phrase,p.match); })) {
            result.knowledge=Knowledge::present; result.value=true; return result;
        }
    }
    if (!uncertain) result.knowledge=Knowledge::absent;
    return result;
}
PromptLandmark hive_proximity_prompt() { return {{"Make Honey","Stop Making Honey","Collect Pollen","Flower Fields"},
    interaction_prompt_search_region,hive_prompt_confidence,PromptMatch::phrase,true}; }
PromptLandmark hive_claim_prompt() { return {{"Claim Hive"},interaction_prompt_search_region,hive_prompt_confidence,PromptMatch::exact,true}; }
}
