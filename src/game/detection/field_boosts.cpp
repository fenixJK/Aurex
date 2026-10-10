#include "field_boosts.hpp"
#include "buff_bar.hpp"
#include "buff_stack.hpp"
#include "field_boost_icons.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace hive::game {
namespace {
bool gold(const Frame& frame,int x,int y) {
    if (x<0 || y<0 || x>=frame.width || y>=frame.height) return false;
    const auto p=(std::size_t(y)*frame.width+x)*4;
    const int r=frame.rgba[p],g=frame.rgba[p+1],b=frame.rgba[p+2];
    return r>100 && g>80 && b<g*.65 && r>g*.8 && r<g*1.65;
}
std::optional<Time> remaining(const Frame& frame,const Rect& cell,double scale) {
    const int bottom=std::lround(cell.y+cell.height)-1,top=std::lround(cell.y);
    const auto height=[&](double edge) {
        int best=0;
        for (int x=std::lround(edge-scale);x<=std::lround(edge+scale);++x) {
            int y=bottom;
            while (y>=bottom-std::ceil(scale) && !gold(frame,x,y)) --y;
            const int end=y;
            while (y>=top && gold(frame,x,y)) --y;
            if (y<end) best=std::max(best,bottom-y);
        }
        return best;
    };
    const int left=height(cell.x),right=height(cell.x+cell.width-scale);
    if (!left || !right || std::abs(left-right)>2*scale) return {};
    return Time(std::lround(field_boost_duration.count()*std::clamp(std::min(left,right)/cell.height,0.,1.)));
}
bool boost_bar(const Frame& frame,const Rect& cell,double scale) {
    unsigned colored=0,total=0;
    for (int y=std::lround(cell.y+cell.height-3*scale);y<int(cell.y+cell.height);++y)
        for (int x=std::max(0,int(cell.x+scale));x<std::min(frame.width,int(cell.x+cell.width-scale));++x) {
            if (y<0 || y>=frame.height) continue;
            colored+=gold(frame,x,y); ++total;
        }
    return total && colored>=total*.6;
}
double difference(const Frame& frame,const detail::FieldIcon& icon,double left,double top,double scale) {
    unsigned error=0,count=0;
    for (int y=0;y<12;++y) for (int x=0;x<16;++x) {
        const auto expected=icon.pixels[y*16+x]; if (expected==0xffffffff) continue;
        const int px=std::lround(left+(2+2*x)*scale),py=std::lround(top+(1+2*y)*scale);
        if (px<0 || py<0 || px>=frame.width || py>=frame.height) return 255;
        const auto p=(std::size_t(py)*frame.width+px)*4;
        for (unsigned c=0;c<3;++c) error+=std::abs(int(frame.rgba[p+c])-int((expected>>(16-8*c))&255));
        ++count;
    }
    return count?error/(3.*count):255;
}
class FieldBoostDetector final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        auto result=std::make_shared<FieldBoostDetection>();
        const auto layout=detail::hud_buff_bar(input); if (!layout) return result;
        const auto pixel_scale=input.frame.width/input.frame.window.bounds.width;
        const auto scale=std::abs(layout->scale-pixel_scale)<pixel_scale*.05?pixel_scale:layout->scale;
        result->readable=true;
        std::array<double,detail::field_icons.size()> best; best.fill(255);
        std::array<Rect,detail::field_icons.size()> cells;
        for (double left=0;left+38*scale<=input.frame.width;left+=std::max(1.,scale)) {
            if (input.cancelled && input.cancelled()) return std::make_shared<FieldBoostDetection>();
            const Rect cell{left,layout->bottom-38*scale,38*scale,38*scale};
            std::array<double,detail::field_icons.size()> scores; scores.fill(255);
            std::array<int,detail::field_icons.size()> offsets{};
            for (int dy=-6;dy<=6;++dy) {
                if (!boost_bar(input.frame,{cell.x-scale,cell.y+(dy-1)*scale,cell.width,cell.height},scale)) continue;
                for (unsigned i=0;i<scores.size();++i) {
                    const auto score=difference(input.frame,detail::field_icons[i],cell.x,cell.y+dy*scale,scale);
                    if (score<scores[i]) { scores[i]=score; offsets[i]=dy; }
                }
            }
            const auto selected=std::min_element(scores.begin(),scores.end()); const auto score=*selected; const auto index=selected-scores.begin();
            *selected=255; const auto second=*std::min_element(scores.begin(),scores.end());
            if (score<28 && second-score>6 && score<best[index]) {
                best[index]=score; cells[index]={cell.x,cell.y+offsets[index]*scale,cell.width,cell.height};
            }
        }
        for (unsigned i=0;i<best.size();++i) if (best[i]<28)
            result->boosts.push_back({std::string(detail::field_icons[i].field),detail::read_buff_stack(input,cells[i],scale,4),remaining(input.frame,{cells[i].x-scale,cells[i].y-scale,cells[i].width,cells[i].height},scale)});
        return result;
    }
};
}
std::shared_ptr<const Detector> field_boost_detector() { return std::make_shared<FieldBoostDetector>(); }
}
