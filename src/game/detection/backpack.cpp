#include "perception.hpp"
#include "menus.hpp"
#include <algorithm>
#include <array>

namespace hive::game {
namespace {
struct BackpackBar { Rect pixels; std::optional<double> percent; };
std::optional<BackpackBar> backpack_bar(const Frame& frame,bool region_only=false,double brightness=1.) {
    if (!frame.valid()) return {};
    const auto pixel=[&](int x,int y) {
        const auto i=(std::size_t(y)*frame.width+x)*4;
        return std::array<int,4>{frame.rgba[i],frame.rgba[i+1],frame.rgba[i+2],frame.rgba[i+3]};
    };
    const auto filled=[brightness](const auto& c) {
        return c[3]>=240 && ((c[1]-c[0]>25*brightness && c[1]-c[2]>30*brightness) || (c[0]-c[1]>25*brightness && c[0]-c[2]>40*brightness)
            || (c[0]-c[2]>35*brightness && c[1]-c[2]>25*brightness));
    };
    const auto empty=[brightness](const auto& c) {
        const auto [lo,hi]=std::minmax({c[0],c[1],c[2]});
        return c[3]>=240 && lo>=55*brightness && hi<=165*brightness && hi-lo<=30*brightness;
    };
    const auto border=[brightness](const auto& c) {
        const auto [lo,hi]=std::minmax({c[0],c[1],c[2]});
        return c[3]>=240 && lo>=165*brightness && c[0]+c[1]+c[2]>=585*brightness && hi-lo<=100*brightness;
    };
    const auto interior=[&](int x,int y) { const auto c=pixel(x,y); return !border(c) && (filled(c) || empty(c)); };
    // Roblox's UI scale is independent of window DPI. Find the bar from its
    // proportions and centered layout, then measure its border and fill.
    const int first=frame.width/2,last=frame.width*9/10;
    const int limit=std::min(frame.height,frame.viewport_height()/5);
    std::optional<BackpackBar> outlined_bar;
    for (int y=1;y<limit;++y) for (int x=first;x<last;) {
        if (!interior(x,y)) { ++x; continue; }
        const int left=x++;
        int right=x;
        // Antialiasing can leave unclassified pixels at the fill boundary.
        // The white outline, rather than that color transition, ends the bar.
        while (x<last && !border(pixel(x,y))) {
            if (interior(x,y)) right=x+1;
            ++x;
        }
        const int width=right-left;
        if (width<frame.width*.04 || width>frame.width*.3
            || left-first<width*.18 || left-first>width*.4) continue;
        const double scale=width/220.;
        const int margin=std::max(1,int(std::round(scale*2)));
        int top=y,bottom=y+1;
        const int inset=std::max(2,int(std::round(width*.025)));
        // Text can reach the trailing edge at some UI scales. Locate the
        // horizontal outline across the bar instead of stopping at a glyph.
        const auto horizontal_edge=[&](int row) {
            unsigned matches=0;
            for (int column=left+inset;column<right-inset;++column) matches+=border(pixel(column,row));
            return matches>=(width-2*inset)*.85;
        };
        while (top>margin && y-top<40*scale && !horizontal_edge(top-1)) --top;
        while (bottom<limit && bottom-top<40*scale && !horizontal_edge(bottom)) ++bottom;
        const int height=bottom-top;
        if (height<24*scale || height>36*scale) continue;
        auto edge=[&](int x,int y,bool horizontal) {
            for (int offset=-margin;offset<=margin;++offset) {
                const int px=x+(horizontal?offset:0),py=y+(horizontal?0:offset);
                if (px>=0 && py>=0 && px<frame.width && py<frame.height && border(pixel(px,py))) return true;
            }
            return false;
        };
        bool outlined=edge(left-margin,(top+bottom)/2,true) && edge(right+margin-1,(top+bottom)/2,true);
        for (int part=0;part<5 && outlined;++part) {
            const int sample=left+inset+(width-2*inset-1)*part/4;
            outlined=edge(sample,top-margin,false) && edge(sample,bottom+margin-1,false);
        }
        // The matching honey box on the other side of the center distinguishes
        // the HUD pair from a similarly colored rectangle in the game world.
        const double half_width=right-frame.width/2.+14*scale;
        unsigned honey_top=0,honey_bottom=0;
        for (double part:{.25,.5,.75}) if (outlined) {
            const int sample=int(frame.width/2.-half_width*part);
            honey_top+=edge(sample,top-margin,false); honey_bottom+=edge(sample,bottom+margin-1,false);
        }
        if (!outlined || honey_top<2 || honey_bottom<2) continue;
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
    const double scale=bar.width/220.,center=frame.width/2.;
    const double width=bar.x+bar.width-center+14*scale;
    const double top=bar.y-5*scale,height=bar.height+10*scale;
    const Rect honey{center-width-6*scale,top,width,height},backpack{center,top,width,height};
    if (honey.x<0 || top<0 || backpack.x+width>frame.width || top+height>frame.height) return {};
    return std::array{honey,backpack};
}
}
std::optional<std::array<Rect,2>> resource_hud_regions(const Frame& frame) {
    const auto bar=backpack_bar(frame,true);
    return bar?resource_regions(frame,bar->pixels):std::nullopt;
}
bool resource_hud_behind_menu(const Frame& frame) {
    // Roblox halves the brightness behind its menu. Keep the same paired-bar
    // geometry checks; this does not make dim game pixels valid during movement.
    return backpack_bar(frame,true,.5).has_value();
}
std::optional<std::array<Rect,2>> resource_hud_regions(const DetectionInput& input) {
    return input.hud?input.hud->resources:resource_hud_regions(input.frame);
}
HudFeatures inspect_hud(const Frame& frame,std::uint64_t generation) { return HudInspector{}.inspect(frame,generation); }
HudFeatures HudInspector::inspect(const Frame& frame,std::uint64_t generation,bool menus,bool backpack) {
    if (frame.window.id!=window_.id || frame.window.process!=window_.process || frame.window.bounds!=window_.bounds || frame.width!=width_ || frame.height!=height_ || generation!=generation_) {
        pollen_activity_={};
        menu_region_.reset(); menu_hints_.clear(); window_=frame.window; width_=frame.width; height_=frame.height; generation_=generation;
    }
    HudFeatures result;
    if (menus) {
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
    }
    result.backpack={Knowledge::unknown,0,frame.captured,frame.window,generation};
    if (backpack && generation) if (const auto bar=backpack_bar(frame)) {
        result.resources=resource_regions(frame,bar->pixels);
        if (bar->percent) { result.backpack.knowledge=Knowledge::present; result.backpack.value=*bar->percent; }
        if (const auto changed=pollen_activity_.observe(frame,bar->pixels))
            result.pollen_changed_at={Knowledge::present,*changed,frame.captured,frame.window,generation};
    }
    if (menus) {
        if (!result.resources) result.resources=resource_hud_regions(frame);
        result.open_menu=read_open_menu(frame,result,generation);
    }
    return result;
}
bool game_hud_visible(const Frame& frame,std::uint64_t generation) {
    if (resource_hud_regions(frame)) return true;
    HudFeatures hud; hud.menus=menu_controls({frame,{},generation,{}});
    return game_hud_visible(hud);
}
bool game_hud_visible(const HudFeatures& hud) {
    return hud.resources.has_value() || hud.backpack.knowledge==Knowledge::present || std::count_if(hud.menus.begin(),hud.menus.end(),
        [](const auto& menu) { return menu.selected.has_value(); })>=2;
}
State read_game_text(const Frame& frame,const std::vector<TextLine>& lines,std::uint64_t generation) {
    State state;
    if (!frame.valid()) return state;
    state.text={Knowledge::present,{frame.width,frame.height,lines},frame.captured,frame.window,generation};
    state.hive_prompt=read_hive_prompt(frame,lines,{frame.width*0.15,0,frame.width*0.7,frame.height*0.3},generation);
    if (state.hive_prompt.knowledge==Knowledge::present) {
        state.at_hive={Knowledge::present,true,frame.captured,frame.window,generation};
        if (state.connection.knowledge==Knowledge::unknown)
            state.connection={Knowledge::present,Connection::connected,frame.captured,frame.window,generation};
    }
    else if (state.hive_prompt.knowledge==Knowledge::absent)
        state.at_hive={Knowledge::absent,false,frame.captured,frame.window,generation};
    return state;
}
}
