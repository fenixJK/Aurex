#include "hive/game/detection/state.hpp"
#include "hive/game/detection/landmarks.hpp"
#include <algorithm>
#include <cctype>

namespace hive::game {
Reading<HivePrompt> read_hive_prompt(const Frame& frame,const std::vector<TextLine>& lines,Rect region,std::uint64_t generation) {
    Reading<HivePrompt> result{Knowledge::unknown,{},frame.captured,frame.window,generation};
    if (!frame.valid() || !region.valid() || region.x<0 || region.y<0 || region.x+region.width>frame.width || region.y+region.height>frame.height) return result;
    const Rect normalized{region.x/frame.width,region.y/frame.height,region.width/frame.width,region.height/frame.height};
    const Context context{frame.window,generation,frame.captured};
    const Reading<TextSnapshot> text{lines.empty()?Knowledge::unknown:Knowledge::present,{frame.width,frame.height,lines},frame.captured,frame.window,generation};
    const auto make=read_prompt_landmark(context,text,{{"Make Honey"},normalized,hive_prompt_confidence});
    const auto stop=read_prompt_landmark(context,text,{{"Stop Making Honey"},normalized,hive_prompt_confidence});
    const auto empty=read_prompt_landmark(context,text,{{"Collect Pollen",
        "To Make Honey, Collect Pollen","To Make Honey, Collect Pollen From Flower Fields","From Flower Fields"},normalized,hive_prompt_confidence});
    const bool a=make.knowledge==Knowledge::present && make.value,b=stop.knowledge==Knowledge::present && stop.value;
    const bool c=empty.knowledge==Knowledge::present && empty.value;
    if (int(a)+int(b)+int(c)==1) {
        result.knowledge=Knowledge::present;
        result.value=a?HivePrompt::make_honey:b?HivePrompt::stop_making_honey:HivePrompt::collect_pollen;
    }
    else if (make.knowledge==Knowledge::absent && stop.knowledge==Knowledge::absent && empty.knowledge==Knowledge::absent) {
        bool partial=false;
        for (const auto& line:lines) {
            const auto& r=line.pixels;
            if (r.x>=region.x+region.width || r.x+r.width<=region.x || r.y>=region.y+region.height || r.y+r.height<=region.y) continue;
            auto value=line.text; std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c) { return std::tolower(c); });
            if (value.find("make")!=std::string::npos || value.find("making")!=std::string::npos || value.find("collect")!=std::string::npos || value=="E" || value=="e") partial=true;
        }
        if (!partial) result.knowledge=Knowledge::absent;
    }
    return result;
}
}
