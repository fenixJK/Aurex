#include "hive/game/detection/field_boosts.hpp"
#include "buff_bar.hpp"
#include "field_boost_icons.hpp"
#include <algorithm>

namespace hive::game {
namespace {
bool boost_bar(const Frame& frame,const Rect& cell,double scale) {
    unsigned gold=0,total=0;
    for (int y=std::lround(cell.y+cell.height-3*scale);y<int(cell.y+cell.height);++y)
        for (int x=std::max(0,int(cell.x+scale));x<std::min(frame.width,int(cell.x+cell.width-scale));++x) {
            if (y<0 || y>=frame.height) continue;
            const auto p=(std::size_t(y)*frame.width+x)*4;
            const int r=frame.rgba[p],g=frame.rgba[p+1],b=frame.rgba[p+2];
            gold+=r>100 && g>80 && b<g*.65 && r>g*.8 && r<g*1.65; ++total;
        }
    return total && gold>=total*.6;
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
        for (double left=0;left+38*scale<=input.frame.width;left+=std::max(1.,scale)) {
            if (input.cancelled && input.cancelled()) return std::make_shared<FieldBoostDetection>();
            const Rect cell{left,layout->bottom-38*scale,38*scale,38*scale};
            std::array<double,detail::field_icons.size()> scores; scores.fill(255);
            for (int dy=-6;dy<=6;++dy) {
                if (!boost_bar(input.frame,{cell.x-scale,cell.y+(dy-1)*scale,cell.width,cell.height},scale)) continue;
                for (unsigned i=0;i<scores.size();++i) scores[i]=std::min(scores[i],difference(input.frame,detail::field_icons[i],
                    cell.x,cell.y+dy*scale,scale));
            }
            const auto selected=std::min_element(scores.begin(),scores.end()); const auto score=*selected; const auto index=selected-scores.begin();
            *selected=255; const auto second=*std::min_element(scores.begin(),scores.end());
            if (score<28 && second-score>6) best[index]=std::min(best[index],score);
        }
        for (unsigned i=0;i<best.size();++i) if (best[i]<28) result->boosts.push_back({std::string(detail::field_icons[i].field)});
        return result;
    }
};
}
std::shared_ptr<const Detector> field_boost_detector() { return std::make_shared<FieldBoostDetector>(); }
}
