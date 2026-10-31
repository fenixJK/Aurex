#include "nectar.hpp"
#include "buff_bar.hpp"
#include <algorithm>

namespace hive::game {
namespace {
int nectar_color(const Frame& frame,int x,int y) { return detail::nectar_color(frame,x,y,20); }
class NectarDetector final : public Detector {
public:
    std::vector<Rect> text_regions(const Frame&,std::uint64_t) const override { return {}; }
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        auto result=std::make_shared<NectarDetection>();
        result->levels=read_nectar(input);
        return result;
    }
};
class ChannelSource final : public NectarSource {
    std::shared_ptr<const DetectionSource> channel_;
    static Observation<NectarLevels> sample(const DetectionSource& channel,const ObservationContext& context) {
        const auto result=channel.read();
        if (!result || !result->error.empty()) return {};
        const auto* value=dynamic_cast<const NectarDetection*>(result->value.get());
        if (!value || !value->levels.has_value(context,Time(2000))) return {};
        const auto& c=result->context;
        if (c.generation!=value->levels.generation || c.now!=value->levels.captured || c.window.id!=value->levels.window.id
            || c.window.process!=value->levels.window.process || c.window.bounds!=value->levels.window.bounds) return {};
        return value->levels;
    }
public:
    explicit ChannelSource(std::shared_ptr<const DetectionSource> channel):channel_(std::move(channel)) {
        if (!channel_) throw std::invalid_argument("Nectar detection channel is missing");
    }
    Observation<NectarLevels> read(const ObservationContext& context) const override {
        return sample(*channel_,context);
    }
};
}
std::size_t NectarLevels::known() const {
    return std::count_if(percent.begin(),percent.end(),[](const auto& value) { return value.has_value(); });
}
namespace {
double remaining_nectar(double percent,Time elapsed) {
    // 100% is 24 hours; one percentage point lasts 864 seconds.
    return std::max(0.,percent-std::max(Time{},elapsed).count()/864000.);
}
}
void NectarTracker::observe(Time captured,const NectarLevels& levels) {
    if (observed_ && captured<=*observed_) return;
    observed_=captured;
    for (std::size_t i=0;i<states_.size();++i) {
        auto& state=states_[i]; const auto value=levels.percent[i];
        if (!value || !std::isfinite(*value) || *value<0 || *value>100
            || !std::isfinite(levels.uncertainty_percent) || levels.uncertainty_percent<0) {
            state.pending.reset(); continue;
        }
        const Reading reading{*value,levels.uncertainty_percent,captured};
        const auto agrees=[&](const Reading& previous) {
            return std::abs(reading.percent-remaining_nectar(previous.percent,captured-previous.at))
                <=std::max(reading.uncertainty,previous.uncertainty);
        };
        if (state.accepted && agrees(*state.accepted)) {
            // Preserve the decay curve within the meter's pixel uncertainty.
            // Replacing it with every quantized reading produces sawtooth spikes.
            state.confirmed=captured; state.pending.reset();
        } else if (state.pending && agrees(*state.pending)) {
            state.accepted=reading; state.confirmed=captured; state.pending.reset();
        } else state.pending=reading;
    }
}
std::array<NectarEstimate,5> NectarTracker::estimate(Time now) const {
    std::array<NectarEstimate,5> result{};
    for (std::size_t i=0;i<states_.size();++i) if (const auto& state=states_[i];state.accepted && now>=state.accepted->at) {
        result[i].percent=remaining_nectar(state.accepted->percent,now-state.accepted->at);
        result[i].estimated=!observed_ || state.confirmed!=*observed_ || now-state.confirmed>Time(5000);
    }
    return result;
}
Observation<NectarLevels> read_nectar(const DetectionInput& input) {
    const auto& f=input.frame;
    Observation<NectarLevels> result{Knowledge::unknown,{},f.captured,f.window,input.generation};
    if (!f.valid() || !input.generation || std::size_t(f.width)*f.height>32000000 || input.text.size()>4096
        || (input.cancelled && input.cancelled())) return result;
    auto layout=detail::hud_buff_bar(input);
    if (!layout && !input.text.empty()) layout=detail::buff_bar(input);
    if (!layout) return result;
    const bool menu_layout=layout->menu_layout;
    const double scale=layout->scale;
    if (scale<.5 || scale>4) return result;
    double pitch=layout->pitch,origin=layout->origin;
    const double expected_bottom=layout->bottom;
    const int top=int(std::floor(expected_bottom-4*scale)),bottom=int(std::ceil(expected_bottom+4*scale));
    if (top<0 || bottom>=f.height) return result;
    std::vector<unsigned> edges(bottom-top+2);
    for (int y=top;y<=bottom;++y) {
        if (input.cancelled && input.cancelled()) return result;
        for (int x=std::max(0,int(std::round(origin)));x<f.width;) {
            const int color=nectar_color(f,x,y),left=x++;
            if (color<0) continue;
            while (x<f.width && nectar_color(f,x,y)==color) ++x;
            const int width=x-left;
            if (width<pitch*.7 || width>pitch*1.3 || y+1>=f.height) continue;
            if (nectar_color(f,left,y+1)==color || nectar_color(f,x-1,y+1)==color) continue;
            ++edges[y+1-top];
        }
    }
    double edge=expected_bottom;
    const auto selected=std::max_element(edges.begin(),edges.end());
    if (*selected) edge=top+(selected-edges.begin());
    const int row_top=int(std::round(edge-pitch)),y=int(edge)-1;
    if (row_top<0 || pitch<15 || pitch>160) return result;
    NectarLevels levels; levels.uncertainty_percent=100*std::max(1.,2*scale)/pitch;
    levels.bar={std::max(0.,origin)/f.width,double(row_top)/f.height,
        (f.width-std::max(0.,origin))/f.width,pitch/f.height};
    std::array<bool,5> ambiguous{};
    const int band=std::max(1,int(std::ceil(2*scale)));
    const auto height=[&](int x,int color) {
        int h=0;
        for (int yy=y;yy>=row_top && nectar_color(f,x,yy)==color;--yy) ++h;
        return h;
    };
    for (int x=std::max(0,int(std::round(origin)));x<f.width;) {
        if (input.cancelled && input.cancelled()) return result;
        const int color=nectar_color(f,x,y),left=x++;
        if (color<0) continue;
        while (x<f.width && nectar_color(f,x,y)==color) ++x;
        if (x-left<band || x-left>pitch+band) continue;
        // Nectar edges belong to the same grid as the other buff icons.
        // Do not measure an arbitrary matching patch between those cells.
        const double cell=(left-origin)/pitch;
        if (std::abs(cell-std::round(cell))*pitch>band+.01) continue;
        const int right=int(std::round(origin+(std::round(cell)+1)*pitch))-1;
        if (right+band>=f.width) continue;
        int a=0,b=0;
        for (int xx=left;xx<=std::min(x-1,left+band);++xx) a=std::max(a,height(xx,color));
        for (int xx=std::max(left,right-band);xx<=right+band;++xx) b=std::max(b,height(xx,color));
        if (!a || !b || std::abs(a-b)>2*scale) continue;
        if (levels.percent[color]) ambiguous[color]=true;
        else levels.percent[color]=std::min(100.,100*std::min(a,b)/pitch);
        x=std::max(x,right+1);
    }
    for (int i=0;i<5;++i) if (ambiguous[i]) levels.percent[i].reset();
    if (!levels.known() && !menu_layout) return result;
    if (input.cancelled && input.cancelled()) return result;
    result.knowledge=Knowledge::present; result.value=levels; return result;
}
std::shared_ptr<const Detector> nectar_detector() { return std::make_shared<NectarDetector>(); }
std::shared_ptr<const NectarSource> nectar_source(std::shared_ptr<const DetectionSource> channel) {
    return std::make_shared<ChannelSource>(std::move(channel));
}
}
