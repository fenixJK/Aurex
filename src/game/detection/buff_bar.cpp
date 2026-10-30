#include "buff_bar.hpp"
#include "menus.hpp"
#include "perception.hpp"
#include "menu_icons.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
namespace hive::game::detail {
constexpr std::array<std::array<int,3>,5> colors{{{126,158,179},{120,179,117},{179,152,167},{147,125,179},{179,89,81}}};
int nectar_color(const Frame& f,int x,int y,int tolerance) {
    if (x<0 || y<0 || x>=f.width || y>=f.height) return -1;
    const auto p=(std::size_t(y)*f.width+x)*4;
    if (f.rgba[p+3]<240) return -1;
    int selected=-1,best=3*tolerance+1;
    for (int i=0;i<5;++i) {
        const int r=std::abs(f.rgba[p]-colors[i][0]),g=std::abs(f.rgba[p+1]-colors[i][1]),b=std::abs(f.rgba[p+2]-colors[i][2]);
        if (std::max({r,g,b})<=tolerance && r+g+b<best) { best=r+g+b; selected=i; }
    }
    return selected;
}
std::string normalized(std::string text) {
    for (auto& c : text) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
std::optional<BuffBar> colored_bar(const DetectionInput& input,const Rect& label) {
    const auto& f=input.frame;
    struct Seed { int left,width,bottom,color; };
    std::vector<Seed> seeds;
    const int top=std::max(0,int(label.y+label.height));
    const int bottom=std::min(f.height-2,int(label.y+label.height*5.5));
    for (int y=top;y<=bottom;++y) {
        if (input.cancelled && input.cancelled()) return {};
        for (int x=0;x<f.width;) {
            const int color=nectar_color(f,x,y),left=x++;
            if (color<0) continue;
            while (x<f.width && nectar_color(f,x,y)==color) ++x;
            const int width=x-left;
            if (width<std::max(4.,label.height*.4) || width>std::min(160.,label.height*4.5)) continue;
            if (nectar_color(f,left,y+1)==color || nectar_color(f,x-1,y+1)==color) continue;
            seeds.push_back({left,width,y+1,color});
            if (seeds.size()>128) return {};
        }
    }
    std::optional<BuffBar> result;
    int best=1;
    double error=std::numeric_limits<double>::max();
    for (const auto& seed : seeds) {
        if (input.cancelled && input.cancelled()) return {};
        if (seed.width<std::max(15.,label.height*1.2)) continue;
        for (double pitch=seed.width;pitch<=seed.width+std::max(2.,seed.width*.08);pitch+=.25) {
            const double scale=pitch/38.;
            if (scale<.5 || scale>4) continue;
            const auto row_top=seed.bottom-pitch;
            if (row_top<label.y+label.height-3*scale || row_top>label.y+label.height+pitch*.8) continue;
            std::array<bool,5> colors_found{},full_colors{};
            double residual=0; unsigned matches=0;
            const auto phase=seed.left-(pitch-seed.width)/2;
            for (const auto& other : seeds) {
                if (std::abs(other.bottom-seed.bottom)>std::max(1.,scale) || other.width>pitch+2*scale) continue;
                const auto distance=[&](double edge) { const auto slots=(edge-phase)/pitch; return std::abs(slots-std::round(slots))*pitch; };
                const auto offset=other.width>=pitch-2*scale
                    ?distance(other.left+other.width/2.-pitch/2.)
                    :std::min(distance(other.left),distance(other.left+other.width));
                if (offset>std::max(.75,scale)) continue;
                colors_found[other.color]=true;
                if (other.width>=pitch-2*scale) full_colors[other.color]=true;
                residual+=offset; ++matches;
            }
            const int count=std::count(colors_found.begin(),colors_found.end(),true);
            if (std::count(full_colors.begin(),full_colors.end(),true)<2 || count<best || (count==best && residual/matches>=error)) continue;
            best=count; error=residual/matches;
            double origin=std::fmod(seed.left-(pitch-seed.width)/2+pitch,pitch);
            if (origin>pitch-2*scale-1) origin-=pitch;
            result=BuffBar{scale,pitch,origin,double(seed.bottom)};
        }
    }
    return result;
}
std::optional<BuffBar> hud_buff_bar(const DetectionInput& input) {
    const auto& f=input.frame;
    if (!f.valid() || !input.generation || std::size_t(f.width)*f.height>32000000
        || (input.cancelled && input.cancelled())) return {};
    if (const auto hud=resource_hud_regions(input)) {
        // Roblox can shrink its HUD independently of the capture/display DPI.
        // The paired resource boxes and buff cells share the same UI scale.
        const double scale=(*hud)[1].width/294.;
        const double bottom=(*hud)[1].y+(*hud)[1].height+37*scale;
        if (bottom>=38*scale && bottom<f.height) return BuffBar{scale,38*scale,0,bottom,true};
    }
    const auto controls=menu_controls(input);
    const MenuControl *inventory=nullptr,*quests=nullptr;
    for (const auto& control : controls) {
        if (control.tab==MenuTab::inventory) inventory=&control;
        if (control.tab==MenuTab::quests) quests=&control;
    }
    if (!inventory && !quests) return {};
    double scale; Point center; double offset;
    if (inventory && quests) {
        const auto a=inventory->bounds,b=quests->bounds;
        const double gap=(quests->center().x-inventory->center().x)*f.width;
        const double size=(a.width+b.width)*f.width/2;
        if (gap<=size*.8 || gap>=size*1.9
            || std::abs(inventory->center().y-quests->center().y)>=std::max(a.height,b.height)*.2) return {};
        scale=gap/55.5; center=quests->center(); offset=85;
    } else {
        const auto& anchor=inventory?*inventory:*quests;
        const auto box=anchor.bounds;
        const double sx=box.width*f.width/(inventory?39:42);
        const double sy=box.height*f.height/(inventory?inventory_icon.size():quest_icon.size());
        if (std::abs(sx-sy)>.15*std::max(sx,sy)) return {};
        scale=(sx+sy)/2; center=anchor.center(); offset=inventory?29.5:85;
    }
    BuffBar bar{scale,38*scale,center.x*f.width-offset*scale,std::round(center.y*f.height-37*scale),true};
    if (scale<.5 || scale>4 || bar.origin < -8*scale || bar.bottom<bar.pitch || bar.bottom>f.height) return {};
    return bar;
}
std::optional<BuffBar> buff_bar(const DetectionInput& input) {
    const auto& f=input.frame;
    if (!f.valid() || !input.generation || std::size_t(f.width)*f.height>32000000 || input.text.size()>4096
        || (input.cancelled && input.cancelled())) return {};
    std::optional<Rect> pollen;
    for (const auto& line : input.text) {
        if (line.text.size()>160 || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.8))) continue;
        const auto label=normalized(line.text);
        if (label!="pollen" && !label.starts_with("pollen ")) continue;
        if (!line.pixels.valid() || line.pixels.x<0 || line.pixels.x+line.pixels.width>f.width || line.pixels.y<0 || line.pixels.y+line.pixels.height>f.height*.3) continue;
        if (pollen) return {};
        pollen=line.pixels;
    }
    if (!pollen) return {};
    auto layout=colored_bar(input,*pollen);
    bool menu_layout=false;
    if (!layout) {
        layout=hud_buff_bar(input);
        if (!layout) return {};
        menu_layout=true;
    }
    if (layout->scale<.5 || layout->scale>4 || layout->origin < -2*layout->scale-1
        || layout->bottom-layout->pitch<pollen->y+pollen->height-3*layout->scale
        || layout->bottom-layout->pitch>pollen->y+pollen->height+layout->pitch*.8) return {};
    layout->menu_layout=menu_layout;
    return layout;
}
}
