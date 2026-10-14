#include "sprinkler_reference.hpp"
#include "nearest_target.hpp"
#include <algorithm>
#include <cmath>

namespace hive::game {
namespace {
bool overlap(const Rect& a,const Rect& b) {
    return a.x<=b.x+b.width && b.x<=a.x+a.width && a.y<=b.y+b.height && b.y<=a.y+a.height;
}
Rect united(const Rect& a,const Rect& b) {
    const auto x=std::min(a.x,b.x),y=std::min(a.y,b.y);
    return {x,y,std::max(a.x+a.width,b.x+b.width)-x,std::max(a.y+a.height,b.y+b.height)-y};
}
bool append(std::vector<Rect>& boxes,Rect box) {
    for (std::size_t i=0;i<boxes.size();) {
        if (overlap(boxes[i],box)) { box=united(boxes[i],box); boxes.erase(boxes.begin()+i); i=0; }
        else ++i;
    }
    if (boxes.size()>=64) return false;
    boxes.push_back(box); return true;
}
class ReferenceDetector final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        return std::make_shared<SprinklerReferences>(find_sprinkler_references(input));
    }
};
class ReferenceSource final : public ScreenReferenceSource {
    std::shared_ptr<const DetectionSource> source_;
public:
    explicit ReferenceSource(std::shared_ptr<const DetectionSource> source):source_(std::move(source)) {
        if (!source_) throw std::invalid_argument("Missing sprinkler reference channel");
    }
    Observation<Point> read(const ObservationContext& context,std::optional<Point> expected) const override {
        const auto packet=source_->read();
        if (!packet || !packet->error.empty()) return {};
        const auto* value=dynamic_cast<const SprinklerReferences*>(packet->value.get());
        if (!value || !value->complete || value->image_width<=0 || value->image_height<=0) return {};
        const auto target=expected.value_or(Point{.5,.5});
        const auto position=[&](const Rect& box) { return Point{(box.x+box.width/2)/value->image_width,(box.y+box.height/2)/value->image_height}; };
        const auto* selected=nearest_target<Rect>(value->candidates,target,expected?.08:.43,.03,position,[&](const Rect& box) {
            return box.valid() && box.x>=0 && box.y>=0 && box.x+box.width<=value->image_width && box.y+box.height<=value->image_height
                && box.width<=value->image_width*.1 && box.height<=value->image_height*.1;
        });
        if (!selected) return {};
        Observation<Point> result{Knowledge::present,position(*selected),
            packet->context.now,packet->context.window,packet->context.generation};
        return result.has_value(context,Time(750))?result:Observation<Point>{};
    }
};
}
SprinklerReferences find_sprinkler_references(const DetectionInput& input) {
    SprinklerReferences result;
    const auto& f=input.frame;
    result.image_width=f.width; result.image_height=f.height;
    if (!f.valid() || !input.generation || !f.window.bounds.valid() || (input.cancelled && input.cancelled())) return result;
    const double sx=f.width/f.window.bounds.width,sy=f.height/f.window.bounds.height;
    if (!std::isfinite(sx) || !std::isfinite(sy) || sx<.5 || sx>4 || sy<.5 || sy>4
        || std::abs(sx-sy)>std::max(sx,sy)*.05 || std::size_t(f.width)*f.height>16000000) return result;
    const int patch_w=std::max(1,int(std::ceil(11*sx))),patch_h=std::max(1,int(std::ceil(13*sy)));
    const int left=int(std::ceil(f.width*.2)),right=int(std::floor(f.width*.8));
    const int top=int(std::ceil(f.height*.2)),bottom=int(std::floor(f.height*.8));
    const int width=right-left;
    if (width<patch_w || bottom-top<patch_h) return result;
    result.search={double(left),double(top),double(width),double(bottom-top)};
    std::vector<unsigned> columns(width);
    std::vector<std::uint8_t> history(std::size_t(width)*patch_h);
    for (int y=top;y<bottom;++y) {
        if (input.cancelled && input.cancelled()) { result.candidates.clear(); return result; }
        const auto row=std::size_t((y-top)%patch_h)*width;
        for (int x=0;x<width;++x) {
            const auto p=(std::size_t(y)*f.width+left+x)*4;
            const int r=f.rgba[p],g=f.rgba[p+1],b=f.rgba[p+2];
            const bool match=f.rgba[p+3]>=240 && r<=51 && std::abs(g-216)<=50 && b>=205;
            columns[x]-=history[row+x]; columns[x]+=match; history[row+x]=match;
        }
        if (y-top+1<patch_h) continue;
        unsigned count=0;
        int run=-1;
        for (int x=0;x<width;++x) {
            count+=columns[x];
            if (x>=patch_w) count-=columns[x-patch_w];
            const bool match=x+1>=patch_w && count*10>=unsigned(patch_w*patch_h)*9;
            if (match && run<0) run=x-patch_w+1;
            if (run>=0 && (!match || x+1==width)) {
                const int end=match?x+1:x;
                if (!append(result.candidates,{double(left+run),double(y-patch_h+1),double(end-run),double(patch_h)})) {
                    result.candidates.clear(); return result;
                }
                run=-1;
            }
        }
    }
    if (input.cancelled && input.cancelled()) { result.candidates.clear(); return result; }
    std::sort(result.candidates.begin(),result.candidates.end(),[](const auto& a,const auto& b) {
        return a.y!=b.y?a.y<b.y:a.x<b.x;
    });
    result.complete=true; return result;
}
std::shared_ptr<const Detector> sprinkler_reference_detector() { return std::make_shared<ReferenceDetector>(); }
std::shared_ptr<const ScreenReferenceSource> sprinkler_reference_source(std::shared_ptr<const DetectionSource> source) {
    return std::make_shared<ReferenceSource>(std::move(source));
}
}
