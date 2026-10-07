#include "hive/game/detection/honey.hpp"
#include "hud_numbers.hpp"
namespace hive::game {
using namespace detail;
Reading<std::uint64_t> read_honey(const Frame& frame,const std::vector<TextLine>& lines,std::uint64_t generation) {
    Reading<std::uint64_t> result{Knowledge::unknown,{},frame.captured,frame.window,generation};
    if (!frame.valid()) return result;
    std::optional<std::uint64_t> found;
    const auto number=[](std::string value) { value.erase(0,value.find_first_not_of(" \t")); value.erase(value.find_last_not_of(" \t")+1); return amount(value,1000000000000000000ULL); };
    for (const auto& label:lines) {
        const auto text=normalize(label.text); const auto& r=label.pixels;
        if (!inside(r,frame) || r.y+r.height>frame.height*.18 || (text!="honey" && !text.starts_with("honey "))) continue;
        std::vector<std::uint64_t> candidates;
        if (text.starts_with("honey ")) { if (auto value=number(text.substr(6))) candidates.push_back(*value); }
        else for (const auto& candidate:lines) {
            const auto& c=candidate.pixels;
            if (!inside(c,frame) || c.x<r.x+r.width-2 || c.x-(r.x+r.width)>r.height*3
                || std::abs((c.y+c.height/2)-(r.y+r.height/2))>std::max(r.height,c.height)*.65) continue;
            if (auto value=number(candidate.text)) candidates.push_back(*value);
        }
        for (const auto value:candidates) { if (found && *found!=value) return result; found=value; }
    }
    if (found) { result.value=*found; result.knowledge=Knowledge::present; }
    return result;
}
Reading<std::uint64_t> HoneyFilter::observe(const ObservationContext& context,Reading<std::uint64_t> reading) {
    if (!reading.has_value(context,Time(2000))) { candidate_={}; return {}; }
    const auto compatible=[&](const Reading<std::uint64_t>& previous) {
        return previous.has_value(context,Time(3000)) && reading.captured>previous.captured
            && std::abs(double(reading.value)-double(previous.value))<=std::max(1.,double(previous.value)*.05);
    };
    const bool confirmed=compatible(candidate_) || compatible(confirmed_);
    candidate_=reading;
    if (confirmed) { confirmed_=reading; return reading; }
    return {};
}
}
