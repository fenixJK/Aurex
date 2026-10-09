#include "game/data/fields.hpp"
#include "catalog.hpp"
#include "detection.hpp"
#include "pixels.hpp"
#include <algorithm>
#include <array>
#include <cctype>

namespace hive::features::quests {
namespace {
std::string normalized(std::string value) {
    std::string result;
    for (unsigned char c : value) {
        if (std::isspace(c)) { if (!result.empty() && result.back()!=' ') result+=' '; }
        else result+=static_cast<char>(std::tolower(c));
    }
    if (!result.empty() && result.back()==' ') result.pop_back();
    return result;
}
std::string giver(const std::string& title) {
    auto name=normalized(title);
    if (name.starts_with("gifted bucko bee") || name.starts_with("gifted riley bee")) name.erase(0,7);
    for (const auto* candidate : {"Black Bear","Brown Bear","Polar Bear","Mother Bear","Panda Bear","Science Bear",
        "Spirit Bear","Dapper Bear","Bee Bear","Gummy Bear","Honey Bee","Bucko Bee","Riley Bee","Onett","Stick Bug"}) {
        const auto prefix=normalized(candidate);
        if (name==prefix) return candidate;
        for (const auto* separator : {": ","'s "}) if (name.starts_with(prefix+separator) && name.size()>prefix.size()+3) return candidate;
    }
    if (const auto match=resolve_quest(title)) return match->giver;
    return {};
}
bool readable(const TextLine& line,const Frame& f) {
    const auto r=line.pixels;
    return r.valid() && r.x>=0 && r.y>=0 && r.x+r.width<=f.width && r.y+r.height<=f.height
        && r.height>=6 && r.height<=f.height/8. && !line.text.empty() && line.text.size()<=80
        && std::none_of(line.text.begin(),line.text.end(),[](unsigned char c) { return c<32 || c==127; });
}
struct Pixels {
    const Frame& frame;
    std::array<int,3> at(int x,int y) const {
        if (x<0 || y<0 || x>=frame.width || y>=frame.height) return {};
        const auto i=(static_cast<std::size_t>(y)*frame.width+x)*4;
        return {frame.rgba[i],frame.rgba[i+1],frame.rgba[i+2]};
    }
    bool title(int x,int y) const {
        const auto c=at(x,y);
        return (c[0]>=190 && c[1]>=200 && c[2]>=200 && std::abs(c[2]-c[0])<=55)
            || (c[1]>=90 && c[1]>c[0]*1.35 && c[1]>c[2]*1.15);
    }
    bool progress(int x,int y) const {
        const auto c=at(x,y);
        return progress_bar_color(c[0],c[1],c[2]);
    }
    bool panel(int x,int y) const {
        const auto c=at(x,y);
        return c[0]>=90 && c[1]>=140 && c[2]>=170 && c[2]>=c[0]*1.2 && c[1]>=c[0]*1.1;
    }
};
struct Header { TextLine text; std::string giver; int left,right; };
std::optional<Header> header(const Pixels& pixels,const TextLine& line) {
    if (!readable(line,pixels.frame)) return {};
    auto owner=giver(line.text);
    if (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.8f || *line.confidence>1)) owner.clear();
    const auto r=line.pixels;
    for (const auto y : {int(std::floor(r.y-r.height*.12)),int(std::ceil(r.y+r.height+r.height*.12))}) {
        const int middle=int(r.x+r.width/2); if (!pixels.title(middle,y)) continue;
        int left=middle,right=middle;
        while (left>0 && pixels.title(left-1,y)) --left;
        while (right<pixels.frame.width && pixels.title(right,y)) ++right;
        const int width=right-left;
        if (left>r.x || right<r.x+r.width || width<r.height*7 || width>r.height*36
            || std::abs((r.x+r.width/2)-(left+right)/2.)>width*.49) continue;
        return Header{line,std::move(owner),left,right};
    }
    return {};
}
bool aligned(const Header& a,const Header& b) {
    const auto tolerance=(a.right-a.left)*.025;
    return std::abs(a.left-b.left)<=tolerance && std::abs(a.right-b.right)<=tolerance;
}
class AutomaticLayout final : public LayoutSource {
public:
    std::vector<CardRegion> locate(const game::DetectionInput& input) const override {
        const auto& f=input.frame;
        if (!f.valid() || input.text.size()>2048) return {};
        const Pixels pixels{f}; std::vector<Header> headers;
        for (const auto& line : input.text) {
            if (input.cancelled && input.cancelled()) return {};
            if (auto found=header(pixels,line)) { headers.push_back(std::move(*found)); if (headers.size()>64) return {}; }
        }
        std::sort(headers.begin(),headers.end(),[](const auto& a,const auto& b) { return std::tie(a.text.pixels.y,a.text.pixels.x)<std::tie(b.text.pixels.y,b.text.pixels.x); });
        std::vector<Header> merged;
        for (auto& h:headers) {
            auto prior=std::find_if(merged.begin(),merged.end(),[&](const auto& p) {
                return aligned(p,h) && std::abs(p.text.pixels.y-h.text.pixels.y)<std::min(p.text.pixels.height,h.text.pixels.height)*.4;
            });
            if (prior==merged.end()) { merged.push_back(std::move(h)); continue; }
            const auto a=prior->text.pixels,b=h.text.pixels;
            if (b.x<a.x) prior->text.text=h.text.text+" "+prior->text.text;
            else prior->text.text+=" "+h.text.text;
            prior->text.pixels={std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x+a.width,b.x+b.width)-std::min(a.x,b.x),std::max(a.y+a.height,b.y+b.height)-std::min(a.y,b.y)};
            if (h.text.confidence) prior->text.confidence=std::min(prior->text.confidence.value_or(1),*h.text.confidence);
            prior->giver=prior->text.confidence.value_or(1)>=.8f?giver(prior->text.text):std::string{};
        }
        headers=std::move(merged);
        std::vector<CardRegion> cards;
        for (const auto& h : headers) {
            const auto r=h.text.pixels; const auto width=h.right-h.left;
            const int left=h.left+int(std::ceil(width*.035)),right=h.right-int(std::ceil(width*.035));
            const int begin=int(std::ceil(r.y+r.height)),gap=std::max(2,int(r.height*.18));
            int limit=f.height;
            for (const auto& next : headers) if (next.text.pixels.y>r.y && aligned(h,next)) { limit=int(next.text.pixels.y); break; }
            auto unit=[&](Rect area) { return Rect{area.x/f.width,area.y/f.height,area.width/f.width,area.height/f.height}; };
            const Rect title{std::max(0.,r.x-.5),std::max(0.,r.y-.5),
                r.width+std::min(.5,r.x)+std::min(.5,f.width-r.x-r.width),
                r.height+std::min(.5,r.y)+std::min(.5,f.height-r.y-r.height)};
            CardRegion card{h.giver,h.text.text,unit(title),{}};
            int top=-1,last=-1,previous=begin,blank=0;
            for (int y=begin;y<=limit;++y) {
                if (input.cancelled && input.cancelled()) return {};
                int colored=0,samples=0;
                if (y<limit) for (int x=left;x<right;x+=std::max(1,(right-left)/64)) { ++samples; colored+=pixels.progress(x,y); }
                int edges=0;
                if (y<limit) for (double part:{.025,.03,.035,.04,.96,.965,.97,.975}) edges+=pixels.progress(left+int((right-left)*part),y);
                const bool bar=(samples && colored>=samples*.35) || edges>=6;
                bool empty=!bar && y<limit;
                if (empty) for (double part:{.025,.04,.96,.975}) empty=empty && pixels.panel(left+int((right-left)*part),y);
                blank=empty?blank+1:0;
                if (bar) { if (top<0) top=y; last=y; }
                if (top>=0 && ((!bar && y-last>gap) || y==limit)) {
                    const int height=last-top+1;
                    if (height<r.height*.9) { top=-1; continue; }
                    if (height>r.height*4.5 || top-previous>r.height*3 || last>=limit-gap) break;
                    int bar_left=h.left,bar_right=h.right;
                    while (bar_left<bar_right && !pixels.progress(bar_left,top)) ++bar_left;
                    while (bar_right>bar_left && !pixels.progress(bar_right-1,top)) --bar_right;
                    if (bar_right-bar_left<width*.5) break;
                    card.rows.push_back({"row-"+std::to_string(card.rows.size()+1),unit({double(bar_left),double(top),double(bar_right-bar_left),double(height)})});
                    if (card.rows.size()==16) break;
                    previous=last+1; top=-1;
                }
                if (top<0 && (y==limit || y-previous>r.height*3)) {
                    card.complete_view=(y==limit && limit<f.height) || blank>r.height*3;
                    break;
                }
            }
            if (!card.rows.empty()) {
                if (!card.giver.empty()) cards.push_back(std::move(card));
            }
        }
        std::map<std::string,unsigned> counts;
        for (const auto& card:cards) ++counts[card.giver];
        std::erase_if(cards,[&](const auto& card) { return counts[card.giver]>1; });
        return cards;
    }
};
}
std::shared_ptr<const LayoutSource> automatic_layout() { return std::make_shared<AutomaticLayout>(); }
std::optional<Rect> quest_progress_area(const game::DetectionInput& input) {
    const auto& f=input.frame;
    if (!f.valid() || input.text.size()>2048) return {};
    const Pixels pixels{f};
    std::optional<Rect> result;
    for (const auto& line:input.text) {
        const auto h=header(pixels,line);
        if (!h || h->right>f.width*.5) continue;
        const int width=h->right-h->left;
        for (int y=int(line.pixels.y+line.pixels.height);y<std::min(f.height,int(line.pixels.y+line.pixels.height*4));++y) {
            if (pixels.progress(h->left+int(width*.08),y) && pixels.progress(h->left+int(width*.92),y))
                return Rect{double(h->left+int(std::ceil(width*.035)))/f.width,double(y)/f.height,
                    double(width-2*int(std::ceil(width*.035)))/f.width,line.pixels.height/f.height};
        }
    }
    for (const auto& line:input.text) {
        if (input.cancelled && input.cancelled()) return {};
        if (!readable(line,f) || line.pixels.x+line.pixels.width>f.width*.5) continue;
        const auto r=line.pixels;
        for (const int y:{int(r.y-r.height*.12),int(r.y+r.height+r.height*.12)}) {
            const int middle=int(r.x+r.width/2);
            if (!pixels.progress(middle,y)) continue;
            int left=middle,right=middle;
            while (left>0 && pixels.progress(left-1,y)) --left;
            while (right<f.width && pixels.progress(right,y)) ++right;
            const int width=right-left;
            if (left>r.x || right<r.x+r.width || width<r.height*7 || width>r.height*36) continue;
            const int inset=std::max(2,int(r.height*.3));
            if (!pixels.panel(std::max(0,left-inset),y)
                || !pixels.panel(std::min(f.width-1,right+inset),y)) continue;
            Rect area{double(left)/f.width,r.y/f.height,double(width)/f.width,r.height/f.height};
            if (result && (std::abs(result->x-area.x)>.03 || std::abs(result->width-area.width)>.03)) return {};
            if (!result) result=area;
            break;
        }
    }
    return result;
}
bool quest_names_complete(const game::DetectionInput& input,const std::vector<CardRegion>& regions) {
    if (!input.frame.valid()) return false;
    const Pixels pixels{input.frame};
    std::vector<Header> known,unknown;
    for (const auto& line:input.text) {
        const auto candidate=header(pixels,line);
        if (!candidate) continue;
        const auto r=line.pixels;
        const bool found=std::any_of(regions.begin(),regions.end(),[&](const auto& card) {
            return !card.giver.empty() && r.x>=card.title.x*input.frame.width && r.y>=card.title.y*input.frame.height
                && r.x+r.width<=(card.title.x+card.title.width)*input.frame.width+1
                && r.y+r.height<=(card.title.y+card.title.height)*input.frame.height+1;
        });
        (found?known:unknown).push_back(*candidate);
    }
    for (const auto& candidate:unknown) {
        if (known.empty() && candidate.right<=input.frame.width*.55) return false;
        if (std::any_of(known.begin(),known.end(),[&](const auto& h) { return aligned(h,candidate); })) return false;
    }
    return true;
}
bool quest_panel_visible(const game::DetectionInput& input) {
    if (!input.frame.valid() || input.text.size()>2048) return false;
    const Pixels pixels{input.frame};
    for (const auto& line : input.text) {
        if (input.cancelled && input.cancelled()) return false;
        if (!giver(line.text).empty() && header(pixels,line)) return true;
    }
    return false;
}
std::vector<Field> default_fields() {
    std::vector<Field> result;
    for (const auto& field : game::fields()) {
        Field value{field.name,{}};
        if (field.id=="bamboo" || field.id=="blueflower" || field.id=="pinetree") value.pollen.insert(Kind::blue_pollen);
        else if (field.id=="mushroom" || field.id=="pepper" || field.id=="rose" || field.id=="strawberry") value.pollen.insert(Kind::red_pollen);
        else if (field.id=="pineapple" || field.id=="spider" || field.id=="sunflower") value.pollen.insert(Kind::white_pollen);
        result.push_back(std::move(value));
    }
    return result;
}
}
