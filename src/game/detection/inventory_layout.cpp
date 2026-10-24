#include "inventory.hpp"
#include "menus.hpp"
#include "text.hpp"
#include <algorithm>
#include <array>
#include <cctype>

namespace hive::game {
namespace {
struct Pixels {
    const Frame& frame;
    std::array<int,3> at(int x,int y) const {
        const auto i=(static_cast<std::size_t>(y)*frame.width+x)*4;
        return {frame.rgba[i],frame.rgba[i+1],frame.rgba[i+2]};
    }
    bool pale(int x,int y) const {
        if (x<0 || y<0 || x>=frame.width || y>=frame.height) return false;
        const auto c=at(x,y); return *std::min_element(c.begin(),c.end())>=195 && *std::max_element(c.begin(),c.end())-*std::min_element(c.begin(),c.end())<=35;
    }
};
struct Card { int left,right,top,bottom; };
bool title(const TextLine& line,const Frame& frame) {
    const auto r=line.pixels;
    if (!r.valid() || r.x<0 || r.y<0 || r.x+r.width>=frame.width || r.y+r.height>=frame.height || r.height<7 || r.height>frame.height/8.0
        || line.text.empty() || line.text.size()>100 || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<0.65f))) return false;
    if (!std::isalpha(static_cast<unsigned char>(line.text[0])) || inventory_name(line.text).empty()) return false;
    // inventory_name validates the supported Unicode punctuation too.
    for (unsigned char c : line.text) if (c<128 && !std::isalnum(c) && c!=' ' && c!='-' && c!='\'') return false;
    return true;
}
int median(std::vector<int> values) { std::sort(values.begin(),values.end()); return values[values.size()/2]; }
std::optional<Card> card(const Pixels& pixels,const TextLine& line) {
    const auto r=line.pixels;
    const int y=static_cast<int>(r.y-r.height*0.18),seed=static_cast<int>(std::ceil(r.x+r.width+r.height*0.12));
    if (!pixels.pale(seed,y)) return {};
    int left=seed,right=seed;
    while (pixels.pale(left-1,y)) --left;
    while (pixels.pale(right,y)) ++right;
    const int width=right-left;
    const double center=(r.x+r.width/2-left)/width;
    if (width<r.height*8 || width>r.height*24 || center<0.58 || center>0.8) return {};
    auto band=[&](int yy) {
        int count=0,total=0;
        for (int x=right-std::max(2,static_cast<int>(width*0.015));x<right;++x) { ++total; count+=pixels.pale(x,yy); }
        return count>=total*0.25;
    };
    int top=y,bottom=y;
    while (top>0 && band(top-1)) --top;
    while (bottom<pixels.frame.height && band(bottom)) ++bottom;
    const int height=bottom-top;
    if (height<width*0.18 || height>width*0.4 || r.y+r.height>bottom || r.y+r.height/2>top+height*0.35) return {};
    const int sample_y=top+std::max(1,static_cast<int>(height*0.08));
    const auto icon=pixels.at(left+static_cast<int>(width*0.28),sample_y),body=pixels.at(left+static_cast<int>(width*0.96),sample_y);
    if (body[0]+body[1]+body[2]-icon[0]-icon[1]-icon[2]<30) return {};
    return Card{left,right,top,bottom};
}
std::optional<InventoryLayout> panel(const Pixels& pixels,const std::vector<Card>& cards) {
    std::vector<int> lefts,rights,heights;
    for (const auto& c : cards) { lefts.push_back(c.left); rights.push_back(c.right); heights.push_back(c.bottom-c.top); }
    const int left=median(lefts),right=median(rights),width=right-left,height=median(heights);
    const int middle=right+std::max(2,static_cast<int>(width*0.025)),outer=right+std::max(3,static_cast<int>(width*0.046));
    const int border=std::max(1,static_cast<int>(width*0.009));
    if (outer+border>=pixels.frame.width || left<0) return {};
    auto track=[&](int y) {
        const auto b=pixels.at(middle,y);
        int edge=765,outside=765;
        for (int x=outer;x<=outer+border;++x) { const auto c=pixels.at(x,y); outside=std::min(outside,c[0]+c[1]+c[2]); }
        for (int x=right;x<=right+std::max(1,static_cast<int>(width*0.009));++x) {
            const auto a=pixels.at(x,y); edge=std::min(edge,a[0]+a[1]+a[2]);
        }
        const auto lo=*std::min_element(b.begin(),b.end()),hi=*std::max_element(b.begin(),b.end());
        return lo>=105 && hi<=250 && hi-lo<=45 && (b[0]+b[1]+b[2]-edge)>=18
            && (b[0]+b[1]+b[2]-outside)>=18;
    };
    const int gap=std::max(2,static_cast<int>(width*0.02));
    const auto anchor=std::find_if(cards.begin(),cards.end(),[&](const Card& c) { return track((c.top+c.bottom)/2); });
    if (anchor==cards.end()) return {};
    const int seed=(anchor->top+anchor->bottom)/2;
    int top=seed,bottom=seed,missed=0;
    for (int y=seed;y>=0;--y) { if (track(y)) { top=y; missed=0; } else if (++missed>gap) break; }
    missed=0;
    for (int y=seed;y<pixels.frame.height;++y) { if (track(y)) { bottom=y+1; missed=0; } else if (++missed>gap) break; }
    if (top==0 || bottom==pixels.frame.height) return {};
    const int track_top=top,track_bottom=bottom;
    for (const auto& c:cards) {
        top=std::min(top,std::max(0,c.top-2)); bottom=std::max(bottom,std::min(pixels.frame.height,c.bottom+2));
    }
    int good=0; std::vector<int> brightness;
    for (int y=top;y<bottom;++y) {
        good+=track(y); const auto c=pixels.at(middle,y),edge=pixels.at(outer,y);
        brightness.push_back((c[0]+c[1]+c[2]-edge[0]-edge[1]-edge[2])/3);
    }
    if (good<height*0.25) return {};
    for (const auto& c : cards) if (c.top<top-gap || c.bottom>bottom+gap || (c.top>top+gap && c.bottom<bottom-gap && std::abs((c.bottom-c.top)-height)>height*0.15)) return {};
    auto sorted=brightness;
    std::sort(sorted.begin(),sorted.end()); const int threshold=sorted[sorted.size()/4]+22;
    std::vector<std::pair<int,int>> thumbs; int start=-1,last=-1;
    for (int y=top;y<=bottom;++y) {
        const bool bright=y<bottom && brightness[y-top]>=threshold;
        if (bright) { if (start<0) start=y; last=y; }
        if (start>=0 && (!bright && (y-last>gap || y==bottom))) {
            if (last-start+1>=std::max(gap*2,(bottom-top)/100)) thumbs.emplace_back(start,last+1);
            start=-1;
        }
    }
    const bool scroll_known=track_top<=top+gap && track_bottom>=bottom-gap
        && thumbs.size()==1 && thumbs[0].second-thumbs[0].first<=(bottom-top)*0.4;
    InventoryLayout layout; layout.open=true;
    layout.viewport={double(left)/pixels.frame.width,double(top)/pixels.frame.height,double(width)/pixels.frame.width,double(bottom-top)/pixels.frame.height};
    layout.at_top=scroll_known && top==track_top && thumbs[0].first-top<=width*0.045;
    layout.at_bottom=scroll_known && bottom==track_bottom && bottom-thumbs[0].second<=width*0.045;
    layout.rows=inventory_rows(pixels.frame,layout.viewport);
    if (layout.rows.size()<2) return {};
    return layout;
}
class LayoutReader final : public InventoryLayoutSource {
public:
    Observation<InventoryLayout> read(const DetectionInput& input) const override {
        auto layout=locate_inventory(input);
        const auto controls=menu_controls(input);
        if (input.cancelled && input.cancelled()) return {};
        for (const auto& control : controls) if (control.tab==MenuTab::inventory) {
            if (layout.knowledge==Knowledge::present) layout.value.toggle=control.center();
            else if (control.selected==false) {
                InventoryLayout closed; closed.toggle=control.center();
                layout={Knowledge::present,std::move(closed),input.frame.captured,input.frame.window,input.generation};
            }
        }
        return layout;
    }
};
}
Observation<InventoryLayout> locate_inventory(const DetectionInput& input) {
    if (!input.frame.valid() || !input.frame.window.id || !input.generation || input.text.size()>2048) throw std::invalid_argument("Invalid inventory layout input");
    if (input.cancelled && input.cancelled()) return {};
    const Pixels pixels{input.frame}; std::vector<std::vector<Card>> groups;
    for (const auto& line : text_candidates(input.text,1)) {
        if (input.cancelled && input.cancelled()) return {};
        if (!title(line,input.frame)) continue;
        const auto found=card(pixels,line); if (!found) continue;
        auto group=std::find_if(groups.begin(),groups.end(),[&](const auto& g) {
            const auto tolerance=(found->right-found->left)*0.02;
            return std::abs(g.front().left-found->left)<=tolerance && std::abs(g.front().right-found->right)<=tolerance;
        });
        if (group==groups.end()) groups.push_back({*found});
        else if (std::none_of(group->begin(),group->end(),[&](const auto& c) { return std::abs(c.top-found->top)<(c.bottom-c.top)/2; })) group->push_back(*found);
    }
    std::optional<InventoryLayout> chosen;
    for (const auto& group : groups) {
        if (input.cancelled && input.cancelled()) return {};
        if (auto candidate=panel(pixels,group)) {
            if (chosen) return {};
            chosen=std::move(candidate);
        }
    }
    if (!chosen || (input.cancelled && input.cancelled())) return {};
    return {Knowledge::present,std::move(*chosen),input.frame.captured,input.frame.window,input.generation};
}
std::shared_ptr<const InventoryLayoutSource> inventory_layout() { return std::make_shared<LayoutReader>(); }
}
