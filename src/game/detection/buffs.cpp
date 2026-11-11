#include "buffs.hpp"
#include "buff_bar.hpp"
#include "status_buff_icons.hpp"
#include "summary_buff_icons.hpp"
#include "buff_stack.hpp"
#include <algorithm>
#include <cmath>

namespace hive::game {
namespace {
using Patch=std::array<std::uint8_t,288>;
constexpr int difference_limit=288*45;
constexpr auto icon_totals=[] {
    std::array<std::array<int,3>,detail::status_buff_icons.size()> totals{};
    for (std::size_t i=0;i<totals.size();++i)
        for (const auto pixel:detail::status_buff_icons[i].pixels)
            for (int c=0;c<3;++c) totals[i][c]+=(pixel>>(16-c*8))&255;
    return totals;
}();
std::optional<double> meter_level(const Frame& frame,Rect cell,double scale,std::array<int,3> color) {
    const auto match=[&](int x,int y) {
        if(x<0 || y<0 || x>=frame.width || y>=frame.height) return false;
        const auto offset=(std::size_t(y)*frame.width+x)*4;
        for(int c=0;c<3;++c) if(std::abs(int(frame.rgba[offset+c])-color[c])>24) return false;
        return true;
    };
    // Read both outer edges of a recognized meter. Artwork through the middle
    // and unrelated blue scenery cannot supply a one-sided fill level.
    const int bottom=std::lround(cell.y+cell.height)-1,top=std::lround(cell.y);
    std::array<int,2> heights{};
    for(int side=0;side<2;++side) {
        const int edge=std::lround(side?cell.x+cell.width-2*scale:cell.x+scale);
        for(int dx=-1;dx<=1;++dx) {
            int gaps=0,height=0;
            for(int y=bottom;y>=top;--y) {
                if(match(edge+dx,y)) {height=bottom-y+1;gaps=0;}
                else if(++gaps>std::max(1,int(std::ceil(scale))))break;
            }
            heights[side]=std::max(heights[side],height);
        }
    }
    if(!heights[0] || !heights[1] || std::abs(heights[0]-heights[1])>3*scale) return {};
    return std::clamp(std::min(heights[0],heights[1])/cell.height,0.,1.);
}
std::optional<Patch> sample(const Frame& frame,double left,double top,double scale,bool summary=false) {
    Patch result{};
    for (int y=0;y<8;++y) for (int x=0;x<12;++x) {
        const double offset=summary?2:3,width=summary?34:32,height=summary?28:21;
        const int x0=std::lround(left+(offset+x*width/12)*scale),y0=std::lround(top+(2+y*height/8)*scale);
        const int x1=std::max(x0+1,int(std::lround(left+(offset+(x+1)*width/12)*scale)));
        const int y1=std::max(y0+1,int(std::lround(top+(2+(y+1)*height/8)*scale)));
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
// Compare the icon's structure independently of each channel's background
// brightness. Countdown shading and the world behind translucent icons change
// their absolute RGB values. Keep a color bound to reject unrelated artwork.
double difference(const Patch& patch,const detail::BuffIcon& icon,const std::array<int,3>& a,const std::array<int,3>& b,double color_limit=45,bool meter=false) {
    double error=0,dot=0,aa=0,bb=0;
    std::array<std::array<double,3>,8> rows_a{},rows_b{};
    if(meter) for(int i=0;i<96;++i) for(int c=0;c<3;++c) {
        rows_a[i/12][c]+=patch[i*3+c]/12.;
        rows_b[i/12][c]+=((icon.pixels[i]>>(16-c*8))&255)/12.;
    }
    for (int i=0;i<96;++i) for (int c=0;c<3;++c) {
        const double x=patch[i*3+c],y=(icon.pixels[i]>>(16-c*8))&255;
        error+=std::abs(x-y);
        const double dx=x-(meter?rows_a[i/12][c]:a[c]/96.),dy=y-(meter?rows_b[i/12][c]:b[c]/96.);
        dot+=dx*dy; aa+=dx*dx; bb+=dy*dy;
    }
    if (aa<1 || bb<1 || error/288>color_limit) return 255;
    return 100*(1-dot/std::sqrt(aa*bb));
}
void read_summary(const DetectionInput& input,const detail::BuffBar& layout,std::vector<BuffState>& buffs) {
    constexpr std::array<std::array<int,3>,5> colors{{{226,172,53},{200,202,60},{72,128,204},{190,162,173},{145,194,253}}};
    const auto& frame=input.frame;
    std::array<double,5> best; best.fill(255);
    std::array<Rect,5> cells{};
    for(int dy=-2;dy<=0;++dy) {
        const double top=layout.bottom-layout.pitch+dy*layout.scale;
        const int y=std::lround(top+37*layout.scale);
        if(y<0 || y>=frame.height) continue;
        for(double left=0;left+layout.pitch<=frame.width;left+=std::max(1.,layout.scale)) {
            if(input.cancelled && input.cancelled()) return;
            std::optional<Patch> patch;
            std::array<int,3> totals{};
            for(std::size_t i=0;i<colors.size();++i) {
                std::array<double,3> edge_error{};
                // A full-body match alone confuses Aura/Blessing and the Pop
                // counter/Bloat. Their bottom-edge colors distinguish them.
                for(int offset:{1,2,3,34,35,36}) {
                    const int x=std::lround(left+offset*layout.scale);
                    if(x<0 || x>=frame.width) {edge_error.fill(255*6);break;}
                    const auto p=(std::size_t(y)*frame.width+x)*4;
                    for(int c=0;c<3;++c) edge_error[c]+=std::abs(int(frame.rgba[p+c])-colors[i][c])/6.;
                }
                if(*std::max_element(edge_error.begin(),edge_error.end())>(i<2?30:i==3?25:18)) continue;
                if(!patch) {
                    patch=sample(frame,left,top,layout.scale,true);
                    if(!patch) break;
                    for(std::size_t k=0;k<patch->size();k+=3)
                        for(int c=0;c<3;++c) totals[c]+=(*patch)[k+c];
                }
                const auto& icon=detail::summary_buff_icons[i]; std::array<int,3> reference{};
                for(const auto p:icon.pixels) for(int c=0;c<3;++c) reference[c]+=(p>>(16-c*8))&255;
                // Meter shading changes entire rows as the fill recedes.
                const double score=difference(*patch,icon,totals,reference,100,i==2 || i==4);
                if(score<icon.maximum_error && score<best[i]) {
                    best[i]=score;cells[i]={left,top,layout.pitch,layout.pitch};
                }
            }
        }
    }
    const auto types=tracked_buffs();
    for(std::size_t i=0;i<best.size();++i) {
        const auto pos=std::find_if(types.begin(),types.end(),[&](const auto& type){return type.name==detail::summary_buff_icons[i].name;});
        if(pos==types.end() || best[i]>=255) continue;
        auto& buff=buffs[pos-types.begin()];buff.active=true;
        if(i==2 || i==4) {
            const auto level=meter_level(frame,cells[i],layout.scale,colors[i]);
            if(level) buff.value=i==2?1+5* *level:1.01+.19* *level;
        } else {
            for(double factor:{1.,.92,1.08}) {
                const auto count=detail::read_buff_stack(input,cells[i],layout.scale*factor,pos->maximum);
                if(!count) continue;
                if(buff.stacks && buff.stacks!=count) {buff.stacks.reset();break;}
                buff.stacks=count;
            }
            buff.value=buff.stacks;
        }
    }
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
        // The HUD establishes the row; avoid matching fragments of nearby icons.
        for (int dy=-2;dy<=2;++dy) {
            if (input.cancelled && input.cancelled()) return result;
            for (double left=0;left+layout->pitch<=frame.width;left+=stride) {
                const auto patch=sample(frame,left,layout->bottom-layout->pitch+dy*layout->scale,layout->scale);
                if (!patch) continue;
                std::array<double,detail::status_buff_icons.size()> errors; errors.fill(255);
                std::array<int,3> totals{};
                for (std::size_t i=0;i<patch->size();i+=3)
                    for (int c=0;c<3;++c) totals[c]+=(*patch)[i+c];
                for (std::size_t i=0;i<index.size();++i) {
                    int minimum_error=0;
                    for (int c=0;c<3;++c) minimum_error+=std::abs(totals[c]-icon_totals[i][c]);
                    if (minimum_error<=difference_limit) errors[index[i]]=std::min(errors[index[i]],difference(*patch,detail::status_buff_icons[i],totals,icon_totals[i]));
                }
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
            if(!types[i].summary_id.empty()) continue;
            result->buffs[i].active=found[i];
            if (!found[i]) result->buffs[i].stacks=0;
            else if (types[i].maximum==1) result->buffs[i].stacks=1;
            else result->buffs[i].stacks=detail::read_buff_stack(input,cells[i],layout->scale,types[i].maximum);
        }
        read_summary(input,*layout,result->buffs);
        if (input.cancelled && input.cancelled()) for (auto& buff:result->buffs) { buff.active.reset(); buff.stacks.reset();buff.value.reset(); }
        return result;
    }
};
}
std::span<const BuffType> tracked_buffs() {
    static constexpr std::array<BuffType,23> types{{
        {"Blue Boost",10},{"Red Boost",10},{"Focus",10},{"Bomb Combo",10},{"Balloon Aura",10},
        {"Baby Love",1},{"Precision",10},{"Inspire",50},{"Honey Mark",3},{"Pollen Mark",3},{"Rage",4},
        {"White Boost",10},{"Pop Star",1},{"Melody",1},{"Jelly Bean Sharing",1},
        {"Guiding Star",1},{"Reindeer Fetch",10},{"Festive Mark",1},
        {"Wealth Clock",5,"clock"},{"Balloon Blessing",100,"balloon"},{"Bubble Bloat",6,"bloat"},
        {"Mondo Blessing",10,"mondo"},{"Tide Blessing",1,"tide"}
    }};
    return types;
}
std::shared_ptr<const Detector> buff_detector() { return std::make_shared<BuffDetector>(); }
std::optional<BuffSnapshot> buff_snapshot(const std::shared_ptr<const DetectionSource>& source,const ObservationContext& context) {
    const auto result=source?source->read():std::nullopt;
    if (!result || !result->error.empty()
        || !Observation<bool>{Knowledge::present,true,result->context.now,result->context.window,result->context.generation}.has_value(context,Time(7000))) return {};
    if (auto value=std::dynamic_pointer_cast<const BuffDetection>(result->value)) return BuffSnapshot{std::move(value),result->context.now};
    return {};
}
}
