#include "hive/game/detection/shift_lock.hpp"
#include "shift_lock_mask.hpp"
#include <algorithm>
namespace hive::game {
Observation<bool> read_shift_lock(const Frame& frame,std::uint64_t generation) {
    Observation<bool> result{Knowledge::unknown,false,frame.captured,frame.window,generation};
    if (!frame.valid() || !generation) return result;
    const double scale=frame.width/frame.window.bounds.width;
    if (scale<.5 || scale>4 || std::abs(frame.height/frame.window.bounds.height-scale)>.05*scale) return result;
    const int edge=std::max(1,int(std::lround(25*scale)));
    const int right=std::min(frame.width-edge,int(65*scale)),top=std::max(0,frame.height-int(70*scale));
    const auto blue=[&](int x,int y) {
        const auto i=(std::size_t(y)*frame.width+x)*4;
        return frame.rgba[i+3]>=240 && frame.rgba[i]<110 && frame.rgba[i]<frame.rgba[i+1]*.7
            && std::abs(int(frame.rgba[i+1])-162)<35 && frame.rgba[i+2]>220;
    };
    bool near=false;
    for (int y=top;y+edge<=frame.height;++y) for (int x=0;x<=right;++x) {
        if (!blue(x+int(8*scale),y) || !blue(x,y+int(10*scale)) || !blue(x+edge-1,y+int(10*scale))
            || blue(x+int(12*scale),y+int(8*scale))) continue;
        unsigned matched=0,expected=0;
        for (int sy=0;sy<25;++sy) for (int sx=0;sx<25;++sx) if (detail::shift_lock_mask[sy][sx]=='1') {
            ++expected; matched+=blue(x+std::min(edge-1,int((sx+.5)*scale)),y+std::min(edge-1,int((sy+.5)*scale)));
        }
        if (matched>=expected*.94) { result.knowledge=Knowledge::present; result.value=true; return result; }
        near|=matched>=expected*.6;
    }
    if (!near) result.knowledge=Knowledge::present;
    return result;
}
}
