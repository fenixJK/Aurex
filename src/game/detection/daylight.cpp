#include "daylight.hpp"
#include "perception.hpp"
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
        const auto hud=resource_hud_regions(input);
        if (!hud) return result;
        const double scale=f.width/f.window.bounds.width;
        unsigned dark=0,blue=0;
        {
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
                    night+=r>=12 && r<=45 && g>=58 && g<=88 && b>=20 && b<=50 && g>r*1.4 && g>b*1.4;
                    // Allow the lighter, less saturated grass in reported views,
                    // while retaining daylight brightness and green dominance.
                    day+=r>=30 && r<=125 && g>=105 && g<=190 && b>=42 && b<=120 && g>r*1.3 && g>b*1.3;
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
Observation<bool> read_night(const DetectionSource& source,const ObservationContext& context,Time since) {
    const auto packet=source.read();
    if (!packet || !packet->error.empty()) return {};
    auto current=context;
    if (!current.window.id) current.window=packet->context.window;
    const Observation<bool> envelope{Knowledge::present,true,packet->context.now,packet->context.window,packet->context.generation};
    if (!envelope.has_value(current,Time(5000),since)) return {};
    const auto reading=std::dynamic_pointer_cast<const Daylight>(packet->value);
    if (!reading || !reading->night) return {};
    auto result=envelope; result.value=*reading->night; return result;
}
std::function<bool(const ObservationContext&)> during_night(std::shared_ptr<const DetectionSource> source) {
    if (!source) return [](const ObservationContext&) { return false; };
    struct State { std::mutex mutex; Observation<bool> night; };
    auto state=std::make_shared<State>();
    return [source=std::move(source),state=std::move(state)](const ObservationContext& context) {
        const auto reading=read_night(*source,context);
        std::lock_guard lock(state->mutex);
        if (reading.knowledge==Knowledge::present && reading.captured>=state->night.captured) state->night=reading;
        auto current=context;
        if (!current.window.id) current.window=state->night.window;
        return state->night.has_value(current,Time(30000)) && state->night.value;
    };
}
}
