#include "hive/game/detection/daylight.hpp"
#include "hive/game/detection/perception.hpp"
#include <algorithm>

namespace hive::game {
namespace {
class DaylightDetector final : public Detector {
public:
    std::vector<Rect> text_regions(const Frame&,std::uint64_t) const override { return {}; }
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        auto result=std::make_shared<Daylight>();
        const auto& f=input.frame;
        if (!f.valid() || !input.generation || std::size_t(f.width)*f.height>32000000) return result;
        const auto hud=resource_hud_regions(f);
        const double scale=f.width/f.window.bounds.width;
        unsigned dark=0,blue=0;
        if (hud) {
            const int top=std::lround((*hud)[1].y+(*hud)[1].height+42*scale);
            const int size=std::max(12,int(std::lround(20*scale)));
            const int bottom=std::min(f.height/3,top+size*4);
            for (int y=top;y+size<=bottom;y+=size) for (int x=f.width*2/5;x+size<f.width*9/10;x+=size) {
                if (input.cancelled && input.cancelled()) return std::make_shared<Daylight>();
                unsigned black=0,sky=0,count=0;
                for (int yy=y;yy<y+size;yy+=2) for (int xx=x;xx<x+size;xx+=2) {
                    const auto p=(std::size_t(yy)*f.width+xx)*4;
                    const int r=f.rgba[p],g=f.rgba[p+1],b=f.rgba[p+2]; ++count;
                    black+=std::max({r,g,b})<35;
                    sky+=b>140 && b-r>35 && g-r>15 && b-g>10;
                }
                dark+=black*100>=count*98; blue+=sky*100>=count*98;
            }
        }
        unsigned night_ground=0,day_ground=0;
        const int patch=std::max(4,int(std::lround(6*scale)));
        for (int y=f.height/2;y+patch<f.height*4/5;y+=patch*2)
            for (int x=f.width/4;x+patch<f.width*9/10;x+=patch*2) {
                if (input.cancelled && input.cancelled()) return std::make_shared<Daylight>();
                unsigned night=0,day=0,count=0;
                for (int yy=y;yy<y+patch;yy+=2) for (int xx=x;xx<x+patch;xx+=2) {
                    const auto p=(std::size_t(yy)*f.width+xx)*4;
                    const int r=f.rgba[p],g=f.rgba[p+1],b=f.rgba[p+2]; ++count;
                    night+=r>=12 && r<=31 && g>=64 && g<=80 && b>=23 && b<=38;
                    day+=r>=30 && r<=85 && g>=105 && g<=175 && b>=42 && b<=100 && g>r*1.8 && g>b*1.5;
                }
                night_ground+=night*100>=count*95;
                day_ground+=day*100>=count*95;
            }
        if (blue>=6 || day_ground>=6) result->night=false;
        else if (night_ground>=6 || dark>=12) result->night=true;
        return result;
    }
};
}
std::shared_ptr<const Detector> daylight_detector() { return std::make_shared<DaylightDetector>(); }
std::function<bool(const ObservationContext&)> during_night(std::shared_ptr<const DetectionSource> source) {
    if (!source) return [](const ObservationContext&) { return false; };
    struct State { std::mutex mutex; Observation<bool> night; };
    auto state=std::make_shared<State>();
    return [source=std::move(source),state=std::move(state)](const ObservationContext& context) {
        const auto packet=source->read();
        std::lock_guard lock(state->mutex);
        if (packet && packet->error.empty() && packet->context.generation==context.generation
            && packet->context.now<=context.now && context.now-packet->context.now<=Time(5000)
            && (!context.window.id || (packet->context.window.id==context.window.id
                && packet->context.window.process==context.window.process && packet->context.window.bounds==context.window.bounds))) {
            const auto reading=std::dynamic_pointer_cast<const Daylight>(packet->value);
            if (reading && reading->night && packet->context.now>=state->night.captured)
                state->night={Knowledge::present,*reading->night,packet->context.now,packet->context.window,packet->context.generation};
        }
        auto current=context;
        if (!current.window.id) current.window=state->night.window;
        return state->night.has_value(current,Time(30000)) && state->night.value;
    };
}
}
