#include "menus.hpp"
#include "perception.hpp"
#include "menu_icons.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace hive::game {
namespace {
struct Mask { MenuTab tab; int width; std::span<const std::uint64_t> rows; };
const std::array masks{Mask{MenuTab::inventory,39,detail::inventory_icon},Mask{MenuTab::quests,42,detail::quest_icon},
    Mask{MenuTab::settings,52,detail::settings_icon}};
bool interior(const Mask& mask,int x,int y) {
    if (x<1 || x+1>=mask.width || y<1 || y+1>=int(mask.rows.size())) return false;
    const auto bit=1ULL<<x;
    return (mask.rows[y]&(bit>>1)) && (mask.rows[y]&(bit<<1))
        && (mask.rows[y-1]&bit) && (mask.rows[y+1]&bit);
}
std::optional<int> icon_brightness(std::vector<int>& light) {
    std::sort(light.begin(),light.end());
    if (light[light.size()*9/10]-light[light.size()/10]>24) return {};
    return light[light.size()/2];
}
struct Candidate { MenuControl control; std::optional<int> brightness; };
}
Point pointer_rest(const ObservationContext& c,const State& state) {
    Point rest{350/c.window.bounds.width,150/c.window.bounds.height};
    if (state.menus.has_value(c,Time(2000))) {
        const auto& menus=state.menus.value;
        const auto inventory=std::find_if(menus.begin(),menus.end(),[](const auto& m) { return m.tab==MenuTab::inventory; });
        const auto quests=std::find_if(menus.begin(),menus.end(),[](const auto& m) { return m.tab==MenuTab::quests; });
        if (inventory!=menus.end() && quests!=menus.end() && quests->center().x>inventory->center().x)
            rest={inventory->center().x+6*(quests->center().x-inventory->center().x),inventory->center().y};
    }
    return {std::clamp(rest.x,0.01,0.95),std::clamp(rest.y,0.01,0.95)};
}
std::vector<MenuControl> menu_controls(const DetectionInput& input,std::optional<Rect> search,std::span<const MenuControl> hints) {
    const auto& f=input.frame;
    if (!f.valid() || !f.window.id || !input.generation || (input.cancelled && input.cancelled())) return {};
    if (input.hud) return input.hud->menus;
    const int width=search?std::clamp(int(std::ceil((search->x+search->width)*f.width)),1,(f.width+1)/2):(f.width+1)/2;
    const int height=search?std::clamp(int(std::ceil((search->y+search->height)*f.height)),1,(f.height+1)/2):(f.height+1)/2;
    const int first_x=search?std::clamp(int(std::floor(search->x*f.width)),0,width-1):0;
    const int first_y=search?std::clamp(int(std::floor(search->y*f.height)),0,height-1):0;
    if (static_cast<std::size_t>(width)*height>8000000) return {};
    auto color=[&](int x,int y) {
        const auto i=(static_cast<std::size_t>(y)*f.width+x)*4;
        return std::array<int,3>{f.rgba[i],f.rgba[i+1],f.rgba[i+2]};
    };
    auto pale=[&](int x,int y) {
        const auto c=color(x,y); const auto lo=*std::min_element(c.begin(),c.end());
        return lo>=200 || (lo>=170 && *std::max_element(c.begin(),c.end())-lo<=35);
    };
    const auto match=[&](const Mask& mask,int left,int top,int w,int h)->std::optional<Candidate> {
        if (left<0 || top<0 || w<1 || h<1 || left+w>f.width || top+h>f.height) return {};
        const int mh=static_cast<int>(mask.rows.size());
        int ink=0,ink_ok=0,holes=0,holes_ok=0; std::vector<int> light;
        for (int yy=0;yy<mh;++yy) for (int xx=0;xx<mask.width;++xx) {
            const int px=left+std::min(w-1,int((xx+.5)*w/mask.width)),py=top+std::min(h-1,int((yy+.5)*h/mh));
            const bool white=pale(px,py);
            if (mask.rows[yy]&(1ULL<<xx)) {
                ++ink; ink_ok+=white;
                if (white && interior(mask,xx,yy)) {
                    const auto c=color(px,py); light.push_back(*std::min_element(c.begin(),c.end()));
                }
            } else { ++holes; holes_ok+=!white; }
        }
        if (ink_ok<ink*(mask.tab==MenuTab::settings?.85:.9)
            || holes_ok<holes*(mask.tab==MenuTab::settings?.7:.5)) return {};
        return Candidate{{mask.tab,{double(left)/f.width,double(top)/f.height,double(w)/f.width,double(h)/f.height},{}},icon_brightness(light)};
    };
    std::vector<Candidate> candidates;
    for (const auto& hint:hints) {
        const auto mask=std::find_if(masks.begin(),masks.end(),[&](const auto& m) { return m.tab==hint.tab; });
        if (mask!=masks.end()) if (const auto found=match(*mask,int(std::lround(hint.bounds.x*f.width)),int(std::lround(hint.bounds.y*f.height)),int(std::lround(hint.bounds.width*f.width)),int(std::lround(hint.bounds.height*f.height)))) candidates.push_back(*found);
    }
    if (candidates.size()!=masks.size()) {
        candidates.clear();
        std::vector<std::uint8_t> visited(static_cast<std::size_t>(width)*height);
        std::vector<int> stack;
        for (int y=first_y;y<height;++y) {
            if (input.cancelled && input.cancelled()) return {};
            for (int x=first_x;x<width;++x) {
                const int start=y*width+x; if (visited[start]) continue;
                visited[start]=1; if (!pale(x,y)) continue;
                stack={start}; int left=x,right=x,top=y,bottom=y,count=0;
                while (!stack.empty()) {
                    const int pos=stack.back(); stack.pop_back(); const int xx=pos%width,yy=pos/width;
                    left=std::min(left,xx); right=std::max(right,xx); top=std::min(top,yy); bottom=std::max(bottom,yy); ++count;
                    if (!(count%4096) && input.cancelled && input.cancelled()) return {};
                    for (const auto d : {std::pair{-1,0},std::pair{1,0},std::pair{0,-1},std::pair{0,1}}) {
                        const int nx=xx+d.first,ny=yy+d.second;
                        if (nx<first_x || ny<first_y || nx>=width || ny>=height) continue;
                        const int next=ny*width+nx; if (visited[next]) continue;
                        visited[next]=1; if (pale(nx,ny)) stack.push_back(next);
                    }
                }
                const int w=right-left+1,h=bottom-top+1;
                if (right+1==width || bottom+1==height) continue;
                if (w<20 || h<18 || w>170 || h>190 || count<w*h*.35) continue;
                for (const auto& mask : masks) {
                    const int mh=static_cast<int>(mask.rows.size());
                    if (std::abs(double(w)/h-double(mask.width)/mh)>.12) continue;
                    if (const auto found=match(mask,left,top,w,h)) candidates.push_back(*found);
                    if (candidates.size()>16) return {};
                }
            }
        }
    }
    const auto unique=[&](MenuTab tab) -> std::optional<Candidate> {
        std::optional<Candidate> result;
        for (const auto& c : candidates) if (c.control.tab==tab) {
            if (result) return {};
            result=c;
        }
        return result;
    };
    auto anchor=unique(MenuTab::inventory);
    if (!anchor) anchor=unique(MenuTab::quests);
    if (!anchor) anchor=unique(MenuTab::settings);
    if (anchor && !(unique(MenuTab::inventory) && unique(MenuTab::quests))) {
        const auto& source=masks[anchor->control.tab==MenuTab::inventory?0:anchor->control.tab==MenuTab::quests?1:2];
        const double scale=anchor->control.tab==MenuTab::settings
            ?std::max(anchor->control.bounds.width*f.width,anchor->control.bounds.height*f.height)/47
            :(anchor->control.bounds.width*f.width/source.width+anchor->control.bounds.height*f.height/source.rows.size())/2;
        const auto slot=[](MenuTab tab) { return tab==MenuTab::inventory?0:tab==MenuTab::quests?1:4; };
        for (const auto& mask : {masks[2],masks[0],masks[1]}) {
            if (unique(mask.tab)) continue;
            const auto center=anchor->control.center();
            const double cx=center.x*f.width+(slot(mask.tab)-slot(anchor->control.tab))*56*scale;
            const double cy=center.y*f.height;
            std::optional<Candidate> best;
            double best_score=0;
            for (double factor : {.8,.85,.9,.95,1.,1.05,1.1}) for (int dy=-5;dy<=5;++dy) for (int dx=-6;dx<=6;++dx) {
                if (input.cancelled && input.cancelled()) return {};
                const int w=std::max(1,int(std::round(mask.width*scale*factor)));
                const int h=std::max(1,int(std::round(mask.rows.size()*scale*factor)));
                const int left=int(std::round(cx-w/2.+dx*scale)),top=int(std::round(cy-h/2.+dy*scale));
                if (left<0 || top<0 || left+w>f.width || top+h>f.height) continue;
                std::vector<int> light,dark;
                for (int y=0;y<int(mask.rows.size());y+=2) for (int x=0;x<mask.width;x+=2) {
                    const int px=left+std::min(w-1,int((x+.5)*w/mask.width));
                    const int py=top+std::min(h-1,int((y+.5)*h/mask.rows.size()));
                    const auto c=color(px,py);
                    (mask.rows[y]&(1ULL<<x)?light:dark).push_back(*std::min_element(c.begin(),c.end()));
                }
                const auto brightness=icon_brightness(light);
                const int threshold=std::max(170,light[light.size()/10]-2);
                const auto ink_ok=std::count_if(light.begin(),light.end(),[&](int value) { return value>=threshold; });
                const auto holes_ok=std::count_if(dark.begin(),dark.end(),[&](int value) { return value<threshold; });
                const double score=double(ink_ok)/light.size()+double(holes_ok)/dark.size();
                if (ink_ok<light.size()*.9 || holes_ok<dark.size()*.5 || score<=best_score) continue;
                best=Candidate{{mask.tab,{double(left)/f.width,double(top)/f.height,double(w)/f.width,double(h)/f.height},{}},brightness};
                best_score=score;
            }
            if (best) candidates.push_back(*best);
        }
    }
    std::vector<MenuControl> result;
    for (const auto& candidate : candidates) {
        if (candidate.control.tab==MenuTab::settings) continue;
        if (std::count_if(candidates.begin(),candidates.end(),[&](const auto& c) { return c.control.tab==candidate.control.tab; })!=1) continue;
        auto control=candidate.control;
        const auto other=std::find_if(candidates.begin(),candidates.end(),[&](const auto& c) {
            return c.control.tab==(control.tab==MenuTab::inventory?MenuTab::quests:MenuTab::inventory);
        });
        if (other!=candidates.end() && std::count_if(candidates.begin(),candidates.end(),[&](const auto& c) { return c.control.tab==other->control.tab; })==1) {
            const auto a=control.bounds,b=other->control.bounds;
            const double gap=(other->control.center().x-control.center().x)*f.width;
            const double size=(a.width+b.width)*f.width/2;
            const bool order=(control.tab==MenuTab::inventory?gap:-gap)>0;
            if (order && std::abs(gap)>size*.8 && std::abs(gap)<size*1.9
                && std::abs(control.center().y-other->control.center().y)<std::max(a.height,b.height)*.2) {
                if (candidate.brightness && *candidate.brightness<=240) control.selected=false;
                else if (candidate.brightness && *candidate.brightness>=248 && other->brightness && *other->brightness<=240) control.selected=true;
            }
        }
        result.push_back(control);
    }
    const auto item=std::find_if(result.begin(),result.end(),[](const auto& c) { return c.tab==MenuTab::inventory; });
    const auto quest=std::find_if(result.begin(),result.end(),[](const auto& c) { return c.tab==MenuTab::quests; });
    if (item!=result.end() && quest!=result.end() && item->selected.has_value() && quest->selected.has_value()) {
        const auto spacing=quest->center().x-item->center().x;
        std::vector<const Candidate*> settings;
        for (const auto& candidate : candidates) {
            const auto& control=candidate.control;
            if (control.tab==MenuTab::settings
                && std::abs(control.center().x-(item->center().x+spacing*4))<spacing*.3
                && std::abs(control.center().y-item->center().y)<std::max(control.bounds.height,item->bounds.height)*.3
                && control.bounds.width/spacing>.6 && control.bounds.width/spacing<1.1) settings.push_back(&candidate);
        }
        if (settings.size()==1) {
            auto control=settings[0]->control;
            if (settings[0]->brightness && *settings[0]->brightness<=240) control.selected=false;
            else if (settings[0]->brightness && *settings[0]->brightness>=248 && !*item->selected && !*quest->selected) control.selected=true;
            result.push_back(control);
        } else if (settings.empty()) {
            const double scale=spacing*f.width/55.5;
            const double cx=(item->center().x+spacing*4)*f.width,cy=(item->center().y+quest->center().y)*f.height/2;
            std::optional<Candidate> best;
            double best_score=0;
            for (int nominal : {48,50,52}) for (int dy=-3;dy<=3;++dy) for (int dx=-2;dx<=2;++dx) {
                if (input.cancelled && input.cancelled()) return {};
                const int size=std::max(1,int(std::round(nominal*scale)));
                const int left=int(std::round(cx-size/2.0+dx*scale)),top=int(std::round(cy-size/2.0+dy*scale));
                if (left<0 || top<0 || left+size>f.width || top+size>f.height) continue;
                int ink=0,ink_ok=0,holes=0,holes_ok=0;
                std::vector<int> light;
                for (int y=0;y<52;++y) for (int x=0;x<52;++x) {
                    const int px=left+std::min(size-1,int((x+.5)*size/52)),py=top+std::min(size-1,int((y+.5)*size/52));
                    const bool white=pale(px,py);
                    if (detail::settings_icon[y]&(1ULL<<x)) {
                        ++ink; ink_ok+=white;
                        if (white) { const auto c=color(px,py); light.push_back(*std::min_element(c.begin(),c.end())); }
                    } else { ++holes; holes_ok+=!white; }
                }
                const double score=double(ink_ok)/ink+double(holes_ok)/holes;
                if (ink_ok<ink*.9 || holes_ok<holes*.7 || score<=best_score) continue;
                const auto brightness=icon_brightness(light);
                best=Candidate{{MenuTab::settings,{double(left)/f.width,double(top)/f.height,double(size)/f.width,double(size)/f.height},{}},brightness};
                best_score=score;
            }
            if (best) {
                if (best->brightness && *best->brightness<=240) best->control.selected=false;
                else if (best->brightness && *best->brightness>=248 && !*item->selected && !*quest->selected) best->control.selected=true;
                result.push_back(best->control);
            }
        }
    }
    if (result.size()<3 && std::none_of(result.begin(),result.end(),[](const auto& control) { return control.selected==true; })) {
        const double dpi=f.width/f.window.bounds.width;
        std::vector<MenuControl> recovered;
        if (dpi>=.5 && dpi<=4) for (const auto& mask:masks) {
            std::optional<MenuControl> best_control;
            double best=0;
            const double center=(mask.tab==MenuTab::inventory?30:mask.tab==MenuTab::quests?85:250)*dpi;
            for (double factor:{.95,1.,1.05}) {
                const double scale=dpi*factor;
                const int w=std::lround(mask.width*scale),h=std::lround(mask.rows.size()*scale);
                const int stride=std::max(1,int(dpi));
                for (int top=int(72*dpi);top<std::min(f.height-h,int(170*dpi));top+=stride)
                    for (int left=int(center-w/2-4*dpi);left<=center-w/2+4*dpi;left+=stride) {
                        if (left<0 || left+w>=f.width) continue;
                        if (input.cancelled && input.cancelled()) return {};
                        int ink=0,white=0,holes=0,dark=0,bright_ink=0,bright_holes=0;
                        std::vector<int> light;
                        for (int y=0;y<int(mask.rows.size());y+=2) for (int x=0;x<mask.width;x+=2) {
                            const auto c=color(left+int((x+.5)*w/mask.width),top+int((y+.5)*h/mask.rows.size()));
                            const auto lo=*std::min_element(c.begin(),c.end());
                            const bool bright=lo>=248;
                            const bool pale=lo>=200 && *std::max_element(c.begin(),c.end())-lo<=60;
                            if (mask.rows[y]&(1ULL<<x)) { ++ink; white+=pale; bright_ink+=bright; light.push_back(lo); }
                            else { ++holes; dark+=!pale; bright_holes+=!bright; }
                        }
                        const bool selected=bright_ink>=ink*.97 && bright_holes>=holes*.5;
                        const double score=selected?double(bright_ink)/ink+double(bright_holes)/holes:double(white)/ink+double(dark)/holes;
                        if ((!selected && (white<ink*.9 || dark<holes*.5)) || score<=best) continue;
                        const auto brightness=icon_brightness(light);
                        std::optional<bool> active;
                        if (selected) active=true;
                        else if (brightness && *brightness<=240) active=false;
                        best=score; best_control=MenuControl{mask.tab,{double(left)/f.width,double(top)/f.height,double(w)/f.width,double(h)/f.height},active};
                    }
            }
            if (best_control) recovered.push_back(*best_control);
        }
        for (const auto& control:recovered) {
            const bool paired=std::any_of(recovered.begin(),recovered.end(),[&](const auto& other) {
                return other.tab!=control.tab && std::abs(other.center().y-control.center().y)*f.height<6*dpi;
            });
            if (!paired && control.selected!=true) continue;
            if (control.selected!=true && std::any_of(result.begin(),result.end(),[&](const auto& current) { return current.tab==control.tab; })) continue;
            std::erase_if(result,[&](const auto& current) { return current.tab==control.tab; });
            result.push_back(control);
        }
    }

    return result;
}
std::optional<Rect> sidebar_region(const DetectionInput& input,MenuTab tab) {
    const auto controls=menu_controls(input);
    for (const auto& control:controls) if (control.tab==tab) {
        double left=control.bounds.x,right=control.bounds.x+control.bounds.width,bottom=control.bounds.y+control.bounds.height;
        for (const auto& other:controls) {
            left=std::min(left,other.bounds.x);right=std::max(right,other.bounds.x+other.bounds.width);
            bottom=std::max(bottom,other.bounds.y+other.bounds.height);
        }
        const double icon=control.bounds.height*input.frame.height/input.frame.width;
        return Rect{std::max(0.,left-icon),bottom,std::min(1.,right+icon*7)-std::max(0.,left-icon),1-bottom};
    }
    return {};
}

}
