#include "planters.hpp"
#include "localization.hpp"
#include "perception.hpp"
#include "bar_regions.hpp"
#include <algorithm>

namespace hive::game {
namespace {
int color(const std::uint8_t* pixel) {
    const int r=pixel[0],g=pixel[1],b=pixel[2];
    const bool filled=(g>=160) & (g-r>=40) & (g-b>=40);
    const bool remaining=(r>=60) & (r<=140) & (g>=85) & (g<=155) & (b>=40) & (b<=130)
        & (g-r>=8) & (g-b>=8);
    return (pixel[3]>=250)*(int(filled)+2*int(remaining));
}
struct Row { int left,right,y; };
bool labelled_growth_bar(const Frame& frame,Rect candidate) {
    const int left=int(candidate.x),right=int(candidate.x+candidate.width);
    const int top=int(candidate.y),bottom=int(candidate.y+candidate.height);
    // Scenery can touch the bar. Its outlined label and straight upper edge
    // identify the UI rectangle without treating that whole contour as the bar.
    const int radius=std::max(1,int(std::ceil(candidate.height*.12)));
    int first_x=right,last_x=left,first_y=bottom,last_y=top,white=0;
    auto labelled=[&] {
        const int width=last_x-first_x+1,height=last_y-first_y+1;
        if (white<8 || height<2 || width<height*8 || white>width*height*.65) return false;
        std::optional<Row> previous;
        unsigned rows=0;
        for (int y=std::max(top,first_y-height*2);y<first_y;++y) {
            const int middle=(first_x+last_x)/2;
            if (!color(frame.rgba.data()+(std::size_t(y)*frame.width+middle)*4)) { rows=0; previous.reset(); continue; }
            int a=middle,b=middle+1;
            while (a>left && color(frame.rgba.data()+(std::size_t(y)*frame.width+a-1)*4)) --a;
            while (b<right && color(frame.rgba.data()+(std::size_t(y)*frame.width+b)*4)) ++b;
            const int span=b-a;
            if (width<span*.35 || first_x<a+span*.05 || last_x>=b-span*.05) { rows=0; previous.reset(); continue; }
            const int tolerance=std::max(1,height/2);
            if (previous && std::abs(previous->left-a)<=tolerance && std::abs(previous->right-b)<=tolerance) ++rows;
            else rows=1;
            previous=Row{a,b,y};
            if (rows>=std::max(1,int(std::ceil(height*.2)))) return true;
        }
        return false;
    };
    for (int y=top;y<=bottom;++y) {
        int row_first=right,row_last=left,row_white=0;
        if (y<bottom) for (int x=left;x<right;++x) {
            const auto* p=frame.rgba.data()+(std::size_t(y)*frame.width+x)*4;
            if (p[3]<250 || std::min({p[0],p[1],p[2]})<120
                || std::max({p[0],p[1],p[2]})-std::min({p[0],p[1],p[2]})>40) continue;
            bool outlined=false;
            for (int dy=-radius;dy<=radius && !outlined;++dy) for (int dx=-radius;dx<=radius;++dx) {
                const int nx=x+dx,ny=y+dy;
                if (nx<left || nx>=right || ny<top || ny>=bottom) continue;
                const auto* edge=frame.rgba.data()+(std::size_t(ny)*frame.width+nx)*4;
                if (std::max({edge[0],edge[1],edge[2]})<100 && edge[1]<p[1]*.65) { outlined=true; break; }
            }
            if (outlined) { ++row_white; row_first=std::min(row_first,x); row_last=std::max(row_last,x); }
        }
        if (row_white>=2) {
            first_x=std::min(first_x,row_first); last_x=std::max(last_x,row_last);
            first_y=std::min(first_y,y); last_y=y; white+=row_white;
        } else {
            if (labelled()) return true;
            first_x=right; last_x=left; first_y=bottom; last_y=top; white=0;
        }
    }
    return false;
}
class PlanterPresenceDetector final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& frame=input.frame;
        if (!frame.valid() || !input.generation || (input.cancelled && input.cancelled())) return {};
        auto result=std::make_shared<Landmarks>();
        auto& visible=result->readings["planter.present"];
        visible={Knowledge::unknown,false,frame.captured,frame.window,input.generation};
        auto& growth=result->readings["planter.growth-bar"];
        growth=visible;
        bool present=false;
        for (const auto& bar:detail::bar_regions(frame,color,true)) {
            if (input.cancelled && input.cancelled()) return {};
            if (labelled_growth_bar(frame,bar)) { present=true; break; }
        }
        const bool readable=present || game_hud_visible(input.hud?*input.hud:inspect_hud(frame,input.generation));
        growth.value=present;
        growth.knowledge=present?Knowledge::present:readable?Knowledge::absent:Knowledge::unknown;
        if (!present) if (const auto region=interaction_prompt_region(frame))
            present=interaction_label(interaction_text_image(frame,*region),"Harvest").has_value();
        if (!present && !readable) return result;
        visible.knowledge=present?Knowledge::present:Knowledge::absent;
        visible.value=present;
        return result;
    }
};
}
std::shared_ptr<const Detector> planter_presence_detector() { return std::make_shared<PlanterPresenceDetector>(); }
}
