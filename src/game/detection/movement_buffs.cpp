#include "movement_buffs.hpp"
#include "buff_bar.hpp"
#include "buff_glyphs.hpp"
#include "haste_icon.hpp"
#include <algorithm>

namespace hive::game {
namespace {
using detail::BuffGlyph;
bool match(const Frame& frame,const BuffGlyph& glyph,int left,int top,double scale) {
    const auto width=std::max(1,int(std::round(glyph.width*scale)));
    const auto height=std::max(1,int(std::round(glyph.height*scale)));
    if (left<0 || top<0 || left+width>frame.width || top+height>frame.height) return false;
    const bool digit=glyph.name.starts_with("Digit");
    const int outline_limit=glyph.name=="Digit1"?200:90;
    const auto matches=[&](bool filtered) {
        unsigned dark=0,compared=0;
        double total_error=0;
        for (int y=0;y<glyph.height;++y) for (int x=0;x<glyph.width;++x) {
            const auto expected=glyph.pixels[y*glyph.width+x];
            const int x0=left+std::min(width-1,int(x*scale)),y0=top+std::min(height-1,int(y*scale));
            const int x1=left+std::min(width,std::max(int(x*scale)+1,int((x+1)*scale)));
            const int y1=top+std::min(height,std::max(int(y*scale)+1,int((y+1)*scale)));
            std::array<double,4> sample{};
            if (!filtered) {
                const auto p=(std::size_t(top+std::min(height-1,int((y+.5)*scale)))*frame.width+left+std::min(width-1,int((x+.5)*scale)))*4;
                for (unsigned c=0;c<4;++c) sample[c]=frame.rgba[p+c];
            } else {
                for (int py=y0;py<y1;++py) for (int px=x0;px<x1;++px) {
                    const auto p=(std::size_t(py)*frame.width+px)*4;
                    for (unsigned c=0;c<4;++c) sample[c]+=frame.rgba[p+c];
                }
                for (auto& value:sample) value/=(x1-x0)*(y1-y0);
            }
            if ((expected&255)==0) {
                if (digit && sample[3]>=240 && sample[0]<=outline_limit && sample[1]<=outline_limit && sample[2]<=outline_limit) ++dark;
                continue;
            }
            if (sample[3]<240) return false;
            for (unsigned c=0;c<3;++c) {
                const auto error=std::abs(sample[c]-int((expected>>(24-c*8))&255));
                if (error>glyph.tolerance+(filtered?20:0)) return false;
                total_error+=error; ++compared;
            }
        }
        if (compared && total_error/compared>glyph.tolerance+(filtered?5:0)) return false;
        return !digit || dark>=std::max(2,glyph.width*glyph.height/12);
    };
    return matches(false) || (scale>=1.5 && glyph.name.starts_with("Bear") && matches(true));
}
std::optional<Rect> locate(const DetectionInput& input,const BuffGlyph& glyph,Rect area,double scale) {
    const int left=std::max(0,int(std::ceil(area.x))),top=std::max(0,int(std::ceil(area.y)));
    const int right=std::min(input.frame.width,int(std::floor(area.x+area.width)))-std::max(1,int(std::round(glyph.width*scale)));
    const int bottom=std::min(input.frame.height,int(std::floor(area.y+area.height)))-std::max(1,int(std::round(glyph.height*scale)));
    for (int x=left;x<=right;++x) {
        if (input.cancelled && input.cancelled()) return {};
        for (int y=bottom;y>=top;--y) if (match(input.frame,glyph,x,y,scale))
            return Rect{double(x),double(y),double(std::max(1,int(std::round(glyph.width*scale)))),double(std::max(1,int(std::round(glyph.height*scale))))};
    }
    return {};
}
const BuffGlyph& glyph(std::string_view name) {
    return *std::find_if(detail::buff_glyphs.begin(),detail::buff_glyphs.end(),[&](const auto& g) { return g.name==name; });
}
}
MovementBuffCandidates find_movement_buffs(const DetectionInput& input) {
    auto layout=detail::hud_buff_bar(input);
    if (!layout && !input.text.empty()) layout=detail::buff_bar(input);
    return layout?detail::movement_buffs_at(input,*layout):MovementBuffCandidates{};
}
MovementBuffCandidates detail::movement_buffs_at(const DetectionInput& input,const BuffBar& bar) {
    MovementBuffCandidates result;
    const auto* layout=&bar;
    if (input.text.empty() && layout->bottom>input.frame.height*.35) return result;
    const double scale=layout->scale,pitch=layout->pitch,top=layout->bottom-pitch;
    if (top<0 || layout->bottom>input.frame.height || pitch<15 || pitch>160) return result;
    const double left=std::max(0.,layout->origin),width=input.frame.width-left;
    if (width<pitch) return result;
    result.bar={left,top,width,std::min(pitch+scale,input.frame.height-top)};
    const Rect stripe{left,layout->bottom-7*scale,width,8*scale};
    for (const auto& g : detail::buff_glyphs) {
        if (input.cancelled && input.cancelled()) return {};
        if (g.name.starts_with("Digit") || g.name=="Haste" || g.name=="Melody") continue;
        if (const auto box=locate(input,g,stripe,scale)) result.candidates.push_back({std::string(g.name),*box,{}});
    }
    Rect haste_area{left,top+18*scale,width,20*scale};
    while (const auto haste=locate(input,glyph("Haste"),haste_area,scale)) {
        const double end=haste_area.x+haste_area.width;
        const double cell_left=haste->x;
        haste_area.x=std::min(end,cell_left+pitch); haste_area.width=end-haste_area.x;
        const Rect cell{cell_left,top,pitch,pitch};
        if (locate(input,glyph("Melody"),{cell_left,top,pitch,haste->y-top},scale)) continue;
        std::optional<unsigned> stacks;
        if (const auto icon=locate(input,detail::unstacked_haste,{haste->x-pitch,top+20*scale,2*pitch,20*scale},scale))
            if (icon->x<=haste->x && icon->x+icon->width>=haste->x+haste->width) stacks=1;
        const Rect digits{cell_left,top,pitch,haste->y-top};
        if (!stacks) for (unsigned digit=9;digit>=2;--digit) if (locate(input,glyph("Digit"+std::to_string(digit)),digits,scale)) { stacks=digit; break; }
        if (!stacks) if (const auto zero=locate(input,glyph("Digit0"),digits,scale)) {
            const Rect tens{std::max(cell_left,zero->x-7*scale),std::max(top,zero->y-4*scale),
                zero->x-std::max(cell_left,zero->x-7*scale),std::min(haste->y,zero->y+zero->height+2*scale)-std::max(top,zero->y-4*scale)};
            if (const auto one=locate(input,glyph("Digit1"),tens,scale))
                if (std::abs(one->y+one->height-zero->y-zero->height)<=2*scale) stacks=10;
        }
        result.candidates.push_back({"Haste",cell,stacks});
        if (result.candidates.size()>64) return {};
    }
    if (input.cancelled && input.cancelled()) return {};
    result.layout_known=true;
    return result;
}
}
