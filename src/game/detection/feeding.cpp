#include "hive/game/detection/feeding.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <regex>

namespace hive::game {
Observation<FeedingDialog> read_feeding_dialog(const ObservationContext& c,const State& state) {
    const auto& observed=state.text;
    if (!observed.has_value(c,Time(2000)) || observed.value.lines.empty() || observed.value.lines.size()>4096 || observed.value.width<=0 || observed.value.height<=0) return {};
    const auto& text=observed.value;
    std::optional<Rect> feed,cancel,input; unsigned amount{}; bool uncertain=false;
    static const std::regex combined("feed +([0-9]{1,7})");
    auto numeric=[&](const std::string& value,unsigned& number) {
        const auto read=std::from_chars(value.data(),value.data()+value.size(),number);
        return !value.empty() && value.size()<=7 && read.ec==std::errc{} && read.ptr==value.data()+value.size() && number<=1000000;
    };
    std::vector<std::pair<Rect,unsigned>> numbers;
    for (const auto& line:text.lines) {
        const auto r=line.pixels;
        if (line.text.size()>512) return {};
        if (!r.valid() || r.x<text.width*.2 || r.x+r.width>text.width*.8 || r.y<text.height*.15 || r.y+r.height>text.height*.9) continue;
        auto value=line.text; std::transform(value.begin(),value.end(),value.begin(),[](unsigned char ch) { return char(std::tolower(ch)); });
        static const std::regex whitespace("\\s+"); value=std::regex_replace(value,whitespace," ");
        if (!value.empty() && value.front()==' ') value.erase(0,1);
        if (!value.empty() && value.back()==' ') value.pop_back();
        const bool label=value.find("feed")!=std::string::npos || value.find("cancel")!=std::string::npos;
        if (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.8f || *line.confidence>1)) { uncertain|=label; continue; }
        std::smatch match; unsigned number{};
        if (value=="feed" || std::regex_match(value,match,combined)) {
            if (feed) return {};
            feed=r;
            if (match.size()>1 && numeric(match[1].str(),amount)) {
                feed->width=r.width*.4; input=Rect{r.x+r.width*.6,r.y,r.width*.4,r.height};
            }
        } else if (value=="cancel") { if (cancel) return {}; cancel=r; }
        else if (numeric(value,number)) numbers.emplace_back(r,number);
        else uncertain|=label;
    }
    if (uncertain) return {};
    if (!feed && !cancel) return {Knowledge::absent,{},observed.captured,c.window,c.generation};
    if (!feed || !cancel) return {};
    if (cancel->y<feed->y || cancel->y-feed->y>feed->height*10 || std::abs(cancel->x-feed->x)>feed->height*6) return {};
    if (!input) for (const auto& [r,n]:numbers) {
        if (r.x<feed->x+feed->width || r.x-feed->x>feed->height*20 || std::abs(r.y-feed->y)>feed->height*.75) continue;
        if (input) return {}; input=r; amount=n;
    }
    if (!input) return {};
    const auto center=[&](Rect r) { return Point{(r.x+r.width/2)/text.width,(r.y+r.height/2)/text.height}; };
    return {Knowledge::present,{center(*feed),center(*cancel),center(*input),amount},observed.captured,c.window,c.generation};
}
}
