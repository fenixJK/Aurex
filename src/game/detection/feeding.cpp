#include "feeding.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <regex>

namespace hive::game {
Observation<FeedingDialog> read_feeding_dialog(const ObservationContext& c,const State& state) {
    const auto& controls=state.fixed_labels;
    if (!controls.has_value(c,Time(2000))) return {};
    std::optional<Rect> feed,cancel,input;
    for (const auto& line:controls.value.lines) {
        if (line.text=="Cancel") { if (cancel) return {}; cancel=line.pixels; }
    }
    if (!cancel) {
        if (std::any_of(controls.value.lines.begin(),controls.value.lines.end(),[](const auto& line) { return line.text=="Feed"; })) return {};
        return {Knowledge::absent,{},controls.captured,c.window,c.generation};
    }
    // The question can also contain "Feed"; the control is the aligned row above Cancel.
    for (const auto& line:controls.value.lines) if (line.text=="Feed") {
        const auto r=line.pixels;
        if (cancel->y<r.y || cancel->y-r.y>r.height*10 || std::abs(cancel->x-r.x)>r.height*6) continue;
        if (!feed || r.y>feed->y) feed=r;
    }
    if (!feed) return {};
    const auto& observed=state.text;
    if (!observed.has_value(c,Time(2000)) || observed.value.lines.size()>4096 || observed.value.width!=controls.value.width
        || observed.value.height!=controls.value.height) return {};
    const auto& text=observed.value;
    unsigned amount{};
    static const std::regex combined("^feed +([0-9]{1,7})$");
    auto numeric=[&](const std::string& value,unsigned& number) {
        const auto read=std::from_chars(value.data(),value.data()+value.size(),number);
        return !value.empty() && value.size()<=7 && read.ec==std::errc{} && read.ptr==value.data()+value.size() && number<=1000000;
    };
    std::vector<std::pair<Rect,unsigned>> numbers;
    for (const auto& line:text.lines) {
        const auto r=line.pixels;
        if (!r.valid() || line.text.size()>512 || r.x<feed->x || r.y<feed->y-feed->height || r.y>feed->y+feed->height) continue;
        if (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.8f || *line.confidence>1)) continue;
        auto value=line.text; std::transform(value.begin(),value.end(),value.begin(),[](unsigned char ch) { return char(std::tolower(ch)); });
        static const std::regex whitespace("\\s+"); value=std::regex_replace(value,whitespace," ");
        if (!value.empty() && value.front()==' ') value.erase(0,1);
        if (!value.empty() && value.back()==' ') value.pop_back();
        std::smatch match; unsigned number{};
        if (std::regex_match(value,match,combined) && numeric(match[1].str(),number)) {
            numbers.emplace_back(Rect{r.x+r.width*.6,r.y,r.width*.4,r.height},number);
        } else if (numeric(value,number)) numbers.emplace_back(r,number);
    }
    if (!input) for (const auto& [r,n]:numbers) {
        if (r.x<feed->x+feed->width || r.x-feed->x>feed->height*20 || std::abs(r.y-feed->y)>feed->height*.75) continue;
        if (input) return {}; input=r; amount=n;
    }
    if (!input) return {};
    const auto center=[&](Rect r) { return Point{(r.x+r.width/2)/text.width,(r.y+r.height/2)/text.height}; };
    return {Knowledge::present,{center(*feed),center(*cancel),center(*input),amount},std::min(observed.captured,controls.captured),c.window,c.generation};
}
}
