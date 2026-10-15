#include "connection.hpp"
#include "perception.hpp"
#include "menus.hpp"
#include <algorithm>
#include <array>

namespace hive::game {
namespace {
struct BackpackBar { Rect pixels; std::optional<double> percent; };
std::optional<BackpackBar> backpack_bar(const Frame& frame,bool region_only=false) {
    if (!frame.valid()) return {};
    const double scale=frame.width/frame.window.bounds.width;
    if (scale<.5 || scale>4) return {};
    const auto pixel=[&](int x,int y) {
        const auto i=(std::size_t(y)*frame.width+x)*4;
        return std::array<int,4>{frame.rgba[i],frame.rgba[i+1],frame.rgba[i+2],frame.rgba[i+3]};
    };
    const auto filled=[](const auto& c) {
        return c[3]>=240 && ((c[1]-c[0]>25 && c[1]-c[2]>30) || (c[0]-c[1]>25 && c[0]-c[2]>40)
            || (c[0]-c[2]>35 && c[1]-c[2]>25));
    };
    const auto empty=[](const auto& c) {
        const auto [lo,hi]=std::minmax({c[0],c[1],c[2]});
        return c[3]>=240 && lo>=55 && hi<=165 && hi-lo<=30;
    };
    const auto border=[](const auto& c) {
        const auto [lo,hi]=std::minmax({c[0],c[1],c[2]});
        return c[3]>=240 && lo>=180 && hi-lo<=65;
    };
    const auto interior=[&](int x,int y) { const auto c=pixel(x,y); return !border(c) && (filled(c) || empty(c)); };
    const int margin=std::max(1,int(std::round(scale*2)));
    const int first=int(frame.width*.5+35*scale),last=std::min(frame.width-margin-1,int(frame.width*.5+330*scale));
    const int limit=std::min(frame.height-margin-1,int(140*scale));
    std::optional<BackpackBar> outlined_bar;
    for (int y=margin;y<limit;++y) for (int x=first;x<last;) {
        if (!interior(x,y)) { ++x; continue; }
        const int left=x++;
        while (x<last && interior(x,y)) ++x;
        const int right=x,width=right-left;
        if (width<190*scale || width>250*scale || left<frame.width*.5+45*scale || left>frame.width*.5+85*scale) continue;
        int top=y,bottom=y+1;
        const int inset=std::max(1,int(scale));
        while (top>margin && y-top<40*scale && interior(left+inset,top-1) && interior(right-1,top-1)) --top;
        while (bottom<limit && bottom-top<40*scale && interior(left+inset,bottom) && interior(right-1,bottom)) ++bottom;
        const int height=bottom-top;
        if (height<24*scale || height>36*scale) continue;
        bool outlined=border(pixel(left-margin,(top+bottom)/2)) && border(pixel(right+margin-1,(top+bottom)/2));
        for (int part=0;part<5 && outlined;++part) {
            const int sample=left+inset+(width-2*inset-1)*part/4;
            outlined=border(pixel(sample,top-margin)) && border(pixel(sample,bottom+margin-1));
        }
        if (!outlined) continue;
        const Rect region{double(left),double(top),double(width),double(height)};
        if (region_only) return BackpackBar{region,0};
        unsigned fill{},valid{},gaps{};
        bool ended=false;
        for (int column=left;column<right;++column) {
            unsigned color{},gray{};
            for (const int row:{top+margin,top+2*margin,bottom-margin-1}) {
                const auto c=pixel(column,row); color+=filled(c); gray+=empty(c);
            }
            if (color>=2) { ++fill; if (ended) ++gaps; }
            else if (gray>=2) { ++valid; ended=true; }
        }
        if (fill+valid<width*.95 || gaps>std::max(2.,width*.015)) {
            if (!outlined_bar) outlined_bar=BackpackBar{region,{}};
            continue;
        }
        return BackpackBar{region,std::clamp(100.*fill/width,0.,100.)};
    }
    return outlined_bar;
}
std::optional<std::array<Rect,2>> resource_regions(const Frame& frame,const Rect& bar) {
    const double scale=frame.width/frame.window.bounds.width,center=frame.width/2.;
    const double width=bar.x+bar.width-center+14*scale;
    const double top=bar.y-5*scale,height=bar.height+10*scale;
    const Rect honey{center-width-6*scale,top,width,height},backpack{center,top,width,height};
    if (honey.x<0 || top<0 || backpack.x+width>frame.width || top+height>frame.height) return {};
    return std::array{honey,backpack};
}
}
Reading<double> read_backpack_percent(const Frame& frame,std::uint64_t generation) {
    Reading<double> result{Knowledge::unknown,0,frame.captured,frame.window,generation};
    if (generation) if (const auto bar=backpack_bar(frame); bar && bar->percent) { result.knowledge=Knowledge::present; result.value=*bar->percent; }
    return result;
}
std::optional<std::array<Rect,2>> resource_hud_regions(const Frame& frame) {
    const auto bar=backpack_bar(frame,true);
    return bar?resource_regions(frame,bar->pixels):std::nullopt;
}
std::optional<std::array<Rect,2>> resource_hud_regions(const DetectionInput& input) {
    return input.hud?input.hud->resources:resource_hud_regions(input.frame);
}
HudFeatures inspect_hud(const Frame& frame,std::uint64_t generation) { return HudInspector{}.inspect(frame,generation); }
HudFeatures HudInspector::inspect(const Frame& frame,std::uint64_t generation) {
    if (frame.window.id!=window_.id || frame.window.process!=window_.process || frame.window.bounds!=window_.bounds || frame.width!=width_ || frame.height!=height_ || generation!=generation_) {
        menu_region_.reset(); menu_hints_.clear(); window_=frame.window; width_=frame.width; height_=frame.height; generation_=generation;
    }
    HudFeatures result;
    if (menu_region_) result.menus=menu_controls({frame,{},generation,{}},menu_region_,menu_hints_);
    if (result.menus.size()!=3) {
        result.menus=menu_controls({frame,{},generation,{}}); menu_region_.reset();
        if (result.menus.size()==3) {
            double left=1,top=1,right=0,bottom=0,pad_x=0,pad_y=0;
            for (const auto& menu:result.menus) {
                left=std::min(left,menu.bounds.x); top=std::min(top,menu.bounds.y);
                right=std::max(right,menu.bounds.x+menu.bounds.width); bottom=std::max(bottom,menu.bounds.y+menu.bounds.height);
                pad_x=std::max(pad_x,menu.bounds.width*.4); pad_y=std::max(pad_y,menu.bounds.height*.4);
            }
            menu_region_=Rect{left-pad_x,top-pad_y,right-left+2*pad_x,bottom-top+2*pad_y};
        }
    }
    menu_hints_=result.menus;
    result.backpack={Knowledge::unknown,0,frame.captured,frame.window,generation};
    if (generation) if (const auto bar=backpack_bar(frame)) {
        result.resources=resource_regions(frame,bar->pixels);
        if (bar->percent) { result.backpack.knowledge=Knowledge::present; result.backpack.value=*bar->percent; }
    }
    return result;
}
bool game_hud_visible(const HudFeatures& hud) {
    return hud.backpack.knowledge==Knowledge::present || std::count_if(hud.menus.begin(),hud.menus.end(),
        [](const auto& menu) { return menu.selected.has_value(); })>=2;
}
State read_game_text(const Frame& frame,const std::vector<TextLine>& lines,std::uint64_t generation) {
    State state;
    if (!frame.valid()) return state;
    state.connection=read_connection(frame,lines,generation);
    state.text={Knowledge::present,{frame.width,frame.height,lines},frame.captured,frame.window,generation};
    state.hive_prompt=read_hive_prompt(frame,lines,{frame.width*0.15,0,frame.width*0.7,frame.height*0.3},generation);
    if (state.hive_prompt.knowledge==Knowledge::present) {
        state.at_hive={Knowledge::present,true,frame.captured,frame.window,generation};
        if (state.connection.knowledge==Knowledge::unknown)
            state.connection={Knowledge::present,Connection::connected,frame.captured,frame.window,generation};
    }
    return state;
}
}
