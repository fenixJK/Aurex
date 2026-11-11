#pragma once
#include "detection.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
namespace hive::game::detail {
struct BuffIcon {
    std::string_view name;
    std::array<std::uint32_t,96> pixels;
    double maximum_error{30};
    unsigned columns{12},rows{8};
    // Zero coverage throughout selects the legacy profile matcher. Otherwise
    // pixels contain straight foreground color and coverage masks the artwork.
    std::array<std::uint8_t,96> coverage{};
    std::uint32_t background{}; // Reference palette, retained with generated masks.
};
using Patch=std::array<std::uint8_t,288>;
inline std::optional<Patch> sample(const Frame& frame,double left,double top,double scale,Rect region={3,2,32,21},unsigned columns=12,unsigned rows=8) {
    if(!columns || !rows || rows>8 || columns>96/rows) return {};
    Patch result{};
    for (unsigned y=0;y<rows;++y) for (unsigned x=0;x<columns;++x) {
        const auto [left_offset,top_offset,width,height]=region;
        const int x0=std::lround(left+(left_offset+x*width/columns)*scale),y0=std::lround(top+(top_offset+y*height/rows)*scale);
        const int x1=std::max(x0+1,int(std::lround(left+(left_offset+(x+1)*width/columns)*scale)));
        const int y1=std::max(y0+1,int(std::lround(top+(top_offset+(y+1)*height/rows)*scale)));
        if (x0<0 || y0<0 || x1>frame.width || y1>frame.height) return {};
        std::array<unsigned,3> pixel{};
        for (int yy=y0;yy<y1;++yy) for (int xx=x0;xx<x1;++xx) {
            const auto p=(std::size_t(yy)*frame.width+xx)*4;
            for (int c=0;c<3;++c) pixel[c]+=frame.rgba[p+c];
        }
        for (int c=0;c<3;++c) result[(y*columns+x)*3+c]=pixel[c]/((x1-x0)*(y1-y0));
    }
    return result;
}
// Compare the icon's structure independently of each channel's background
// brightness. Countdown shading and the world behind translucent icons change
// their absolute RGB values. Keep a color bound to reject unrelated artwork.
inline double difference(const Patch& patch,const BuffIcon& icon,const std::array<int,3>& a,const std::array<int,3>& b,double color_limit=45,bool meter=false,bool brightness=false) {
    if(!icon.columns || !icon.rows || icon.rows>8 || icon.columns>96/icon.rows) return 255;
    double error=0,dot=0,aa=0,bb=0;
    const unsigned count=icon.columns*icon.rows;
    std::array<std::array<double,3>,8> rows_a{},rows_b{};
    if(meter) for(int i=0;i<count;++i) for(int c=0;c<3;++c) {
        rows_a[i/icon.columns][c]+=patch[i*3+c]/double(icon.columns);
        rows_b[i/icon.columns][c]+=((icon.pixels[i]>>(16-c*8))&255)/double(icon.columns);
    }
    for (int i=0;i<count;++i) for (int c=0;c<3;++c) {
        const double x=patch[i*3+c],y=(icon.pixels[i]>>(16-c*8))&255;
        error+=std::abs(x-y);
        const double dx=x-(meter?rows_a[i/icon.columns][c]:(brightness?(a[0]+a[1]+a[2])/3.:a[c])/double(count)),dy=y-(meter?rows_b[i/icon.columns][c]:(brightness?(b[0]+b[1]+b[2])/3.:b[c])/double(count));
        dot+=dx*dy; aa+=dx*dx; bb+=dy*dy;
    }
    if (aa<1 || bb<1 || error/(count*3)>color_limit) return 255;
    return 100*(1-dot/std::sqrt(aa*bb));
}
}
