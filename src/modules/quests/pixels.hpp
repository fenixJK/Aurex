#pragma once
#include "hive/core/model.hpp"
#include <algorithm>
#include <array>

namespace hive::features::quests {
enum class ProgressColor { unknown, incomplete, complete };
inline bool progress_bar_color(int red,int green,int blue) {
    return (red>=170 && green>=65 && blue>=50 && red>green*1.35 && red>blue*1.45)
        || (green>=210 && green>red*1.25 && green>blue*1.05);
}
inline ProgressColor progress_color(int red,int green,int blue) {
    if (red>=170 && green>=65 && blue>=50 && red>green*1.35 && red>blue*1.45)
        return ProgressColor::incomplete;
    if (green<195 || green<red*1.25) return ProgressColor::unknown;
    if (blue<green*.475 && blue<=red+10) return ProgressColor::incomplete;
    if (blue>=green*.48 && blue<=green*.75) return ProgressColor::complete;
    return ProgressColor::unknown;
}
inline ProgressColor progress_state(const Frame& frame,Rect area) {
    std::array<unsigned,4> known{}; unsigned complete=0,incomplete=0;
    const std::array xs{.025,.04,.96,.975},ys{.12,.2,.8,.88};
    for (unsigned y=0;y<ys.size();++y) for (unsigned x=0;x<xs.size();++x) {
        const int px=std::clamp(int((area.x+area.width*xs[x])*frame.width),0,frame.width-1);
        const int py=std::clamp(int((area.y+area.height*ys[y])*frame.height),0,frame.height-1);
        const auto i=(std::size_t(py)*frame.width+px)*4;
        const auto color=progress_color(frame.rgba[i],frame.rgba[i+1],frame.rgba[i+2]);
        known[(y/2)*2+x/2]+=color!=ProgressColor::unknown;
        complete+=color==ProgressColor::complete; incomplete+=color==ProgressColor::incomplete;
    }
    if (complete>=12 && !incomplete && std::all_of(known.begin(),known.end(),[](unsigned n) { return n>=2; })) return ProgressColor::complete;
    if (incomplete>=8 && incomplete>=complete*3 && std::all_of(known.begin(),known.end(),[](unsigned n) { return n>=1; })) return ProgressColor::incomplete;
    return ProgressColor::unknown;
}
}
