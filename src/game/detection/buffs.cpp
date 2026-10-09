#include "buffs.hpp"
#include "buff_bar.hpp"
#include "status_buff_icons.hpp"
#include "buff_stack.hpp"
#include <algorithm>

namespace hive::game {
namespace {
using Patch=std::array<std::uint8_t,288>;
std::optional<Patch> sample(const Frame& frame,double left,double top,double scale) {
    Patch result{};
    for (int y=0;y<8;++y) for (int x=0;x<12;++x) {
        const int x0=std::lround(left+(3+x*32./12)*scale),y0=std::lround(top+(2+y*21./8)*scale);
        const int x1=std::max(x0+1,int(std::lround(left+(3+(x+1)*32./12)*scale)));
        const int y1=std::max(y0+1,int(std::lround(top+(2+(y+1)*21./8)*scale)));
        if (x0<0 || y0<0 || x1>frame.width || y1>frame.height) return {};
        std::array<unsigned,3> pixel{};
        for (int yy=y0;yy<y1;++yy) for (int xx=x0;xx<x1;++xx) {
            const auto p=(std::size_t(yy)*frame.width+xx)*4;
            for (int c=0;c<3;++c) pixel[c]+=frame.rgba[p+c];
        }
        for (int c=0;c<3;++c) result[(y*12+x)*3+c]=pixel[c]/((x1-x0)*(y1-y0));
    }
    return result;
}
double difference(const Patch& patch,const detail::BuffIcon& icon) {
    unsigned error=0;
    for (int i=0;i<96;++i) for (int c=0;c<3;++c) {
        error+=std::abs(int(patch[i*3+c])-int((icon.pixels[i]>>(16-c*8))&255));
        if (error>288*40) return 255;
    }
    return error/288.;
}
class BuffDetector final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        auto result=std::make_shared<BuffDetection>();
        const auto types=tracked_buffs();
        for (const auto& type:types) result->buffs.push_back({std::string(type.name),{}});
        std::array<std::size_t,detail::status_buff_icons.size()> index{};
        for (std::size_t i=0;i<index.size();++i) index[i]=std::find_if(result->buffs.begin(),result->buffs.end(),[&](const auto& buff) { return buff.name==detail::status_buff_icons[i].name; })-result->buffs.begin();
        std::array<double,detail::status_buff_icons.size()> limits{};
        for (std::size_t i=0;i<index.size();++i) limits[index[i]]=detail::status_buff_icons[i].maximum_error;
        const auto layout=detail::hud_buff_bar(input);
        if (!layout) return result;
        const auto& frame=input.frame;
        std::vector<bool> found(result->buffs.size());
        std::vector<double> best_scores(result->buffs.size(),255);
        std::vector<Rect> cells(result->buffs.size());
        const double stride=layout->scale<1?1.:2*layout->scale;
        for (int dy=-6;dy<=6;++dy) {
            if (input.cancelled && input.cancelled()) return result;
            for (double left=0;left+layout->pitch<=frame.width;left+=stride) {
                const auto patch=sample(frame,left,layout->bottom-layout->pitch+dy*layout->scale,layout->scale);
                if (!patch) continue;
                std::array<double,detail::status_buff_icons.size()> errors; errors.fill(255);
                for (std::size_t i=0;i<index.size();++i) errors[index[i]]=std::min(errors[index[i]],difference(*patch,detail::status_buff_icons[i]));
                const auto first=std::min_element(errors.begin(),errors.end());
                const auto selected=std::size_t(first-errors.begin()); const auto best=*first;
                *first=255; const auto second=*std::min_element(errors.begin(),errors.end());
                if (best<limits[selected] && second-best>5) {
                    found[selected]=true;
                    if (best<best_scores[selected]) {
                        best_scores[selected]=best;
                        cells[selected]={left,layout->bottom-layout->pitch+dy*layout->scale,layout->pitch,layout->pitch};
                    }
                }
            }
        }
        for (std::size_t i=0;i<found.size();++i) {
            result->buffs[i].active=found[i];
            if (!found[i]) result->buffs[i].stacks=0;
            else if (types[i].maximum==1) result->buffs[i].stacks=1;
            else result->buffs[i].stacks=detail::read_buff_stack(input,cells[i],layout->scale,types[i].maximum);
        }
        if (input.cancelled && input.cancelled()) for (auto& buff:result->buffs) { buff.active.reset(); buff.stacks.reset(); }
        return result;
    }
};
}
std::span<const BuffType> tracked_buffs() {
    static constexpr std::array<BuffType,18> types{{
        {"Blue Boost",10},{"Red Boost",10},{"Focus",10},{"Bomb Combo",10},{"Balloon Aura",10},
        {"Baby Love",1},{"Precision",10},{"Inspire",50},{"Honey Mark",3},{"Pollen Mark",3},{"Rage",4},
        {"White Boost",10},{"Pop Star",1},{"Melody",1},{"Jelly Bean Sharing",1},
        {"Guiding Star",1},{"Reindeer Fetch",10},{"Festive Mark",1}
    }};
    return types;
}
std::shared_ptr<const Detector> buff_detector() { return std::make_shared<BuffDetector>(); }
}
