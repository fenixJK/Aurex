#include "hive/game/detection/hive_view.hpp"
#include <algorithm>
#include <array>
#include <mutex>

namespace hive::game {
namespace {
constexpr std::array<std::uint32_t,10> hive_palette{
    0xd28f0c,0xc08200,0x916507,0x845c00,0xb97e03,
    0xaa7400,0xffb325,0x694a00,0xe39d1f,0xa28645
};
constexpr std::array<std::uint32_t,6> spawn_palette{
    0x937b1f,0x796615,0xc1ae55,0x957a70,0x988a55,0xc3bca5
};
std::optional<Rect> honeycomb_region(const Frame& f,double scale,double scale_y) {
    const int x_step=std::max(1,static_cast<int>(std::lround(2*scale)));
    const int y_step=std::max(1,static_cast<int>(std::lround(6*scale_y)));
    const int left=f.width/5,right=f.width*4/5;
    int first_y=f.height,last_y=0,first_x=f.width,last_x=0,rows=0;
    for (int y=f.height/8;y<f.height*3/4;y+=y_step) {
        int gold=0,changes=0,row_left=f.width,row_right=0;
        bool previous=false;
        for (int x=left;x<right;x+=x_step) {
            const auto offset=(static_cast<std::size_t>(y)*f.width+x)*4;
            const int red=f.rgba[offset],green=f.rgba[offset+1],blue=f.rgba[offset+2];
            const bool current=f.rgba[offset+3]>=251 && red>=140 && green*100>=red*63
                && green*100<=red*105 && blue*100<=red*52;
            if (current) { ++gold; row_left=std::min(row_left,x); row_right=x; }
            if (current!=previous) ++changes;
            previous=current;
        }
        const int samples=(right-left+x_step-1)/x_step;
        if (changes<8 || gold<samples/10 || gold>samples*2/3) continue;
        ++rows; first_y=std::min(first_y,y); last_y=y;
        first_x=std::min(first_x,row_left); last_x=std::max(last_x,row_right);
    }
    if (rows<8 || last_y-first_y<f.height*.14 || first_x>f.width*.43
        || last_x<f.width*.57 || last_x-first_x<f.width*.28) return {};
    return Rect{double(first_x)/f.width,double(first_y)/f.height,
        double(last_x-first_x+x_step)/f.width,double(last_y-first_y+y_step)/f.height};
}
bool centered_hive(const Frame& f,double scale,double scale_y) {
    const int x_step=std::max(1,static_cast<int>(std::lround(4*scale)));
    const int y_step=std::max(1,static_cast<int>(std::lround(4*scale_y)));
    std::size_t samples=0,cool=0,dark=0,gold=0;
    for (int y=static_cast<int>(f.height*.13);y<static_cast<int>(f.height*.32);y+=y_step)
        for (int x=static_cast<int>(f.width*.45);x<static_cast<int>(f.width*.55);x+=x_step) {
            const auto offset=(static_cast<std::size_t>(y)*f.width+x)*4;
            if (f.rgba[offset+3]<251) continue;
            const int red=f.rgba[offset],green=f.rgba[offset+1],blue=f.rgba[offset+2];
            ++samples;
            if (green>130 && blue>110 && blue*100>red*85 && green*100>red*70) ++cool;
            if (red<100 && green<100 && blue<100) ++dark;
            if (red>=140 && green*100>=red*63 && green*100<=red*105 && blue*100<=red*52) ++gold;
        }
    return samples && cool*10>=samples && gold*12>=samples && cool>=2*dark;
}
template<std::size_t N, bool TexturedGold=false, bool SpawnGold=false>
class FlatView final : public Detector {
    std::array<std::uint32_t,N> palette_;
    std::string id_;
    int quarter_,tolerance_;
    std::array<int,3> low_{255,255,255},high_{};
public:
    FlatView(std::array<std::uint32_t,N> palette,std::string id,int quarter,int tolerance)
        :palette_(palette),id_(std::move(id)),quarter_(quarter),tolerance_(tolerance) {
        for (auto rgb : palette_) for (int c=0;c<3;++c) {
            const int channel=(rgb>>(16-8*c))&255;
            low_[c]=std::min(low_[c],channel-tolerance_); high_[c]=std::max(high_[c],channel+tolerance_);
        }
    }
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& f=input.frame;
        if (!f.valid() || !input.generation || (input.cancelled && input.cancelled())) return {};
        auto result=std::make_shared<HiveViewDetection>();
        auto& reading=result->readings[id_];
        reading={Knowledge::unknown,false,f.captured,f.window,input.generation};
        result->region={Knowledge::unknown,{},f.captured,f.window,input.generation};
        const double scale=f.width/f.window.bounds.width,scale_y=f.height/f.window.bounds.height;
        if (scale<.5 || scale>4 || scale_y<.5 || scale_y>4 || std::abs(scale-scale_y)>.1
            || f.width>8192 || f.height>8192 || f.window.bounds.width<320 || f.window.bounds.height<200) return result;
        if constexpr (TexturedGold && !SpawnGold)
            result->facing={Knowledge::present,centered_hive(f,scale,scale_y),f.captured,f.window,input.generation};
        const int width=std::max(1,static_cast<int>(std::lround(22*scale)));
        const int height=std::max(1,static_cast<int>(std::lround(4*scale_y)));
        const int top=f.height*quarter_/4, bottom=f.height*(quarter_+1)/4;
        const std::size_t required=std::max<std::size_t>(1,static_cast<std::size_t>(f.width)*f.width/3200);
        struct Column { int row{-2},height{}; };
        constexpr std::size_t count=N+(TexturedGold?1:0);
        const int gold_width=std::max(1,static_cast<int>(std::lround(SpawnGold?64*scale:22*scale)));
        const int gold_height=std::max(1,static_cast<int>(std::lround(SpawnGold?16*scale_y:4*scale_y)));
        std::vector<Column> columns(static_cast<std::size_t>(f.width)*count);
        std::array<std::size_t,count> matches{};
        std::vector<bool> gold_columns(SpawnGold?f.width:0);
        struct Bounds { int left,top,right{},bottom{}; };
        std::array<Bounds,count> bounds;
        for (auto& box : bounds) box={f.width,f.height};
        for (int y=top;y<bottom;++y) {
            if (input.cancelled && input.cancelled()) return {};
            unsigned previous=0;
            std::array<int,count> runs{};
            for (int x=0;x<f.width;++x) {
                const auto offset=(static_cast<std::size_t>(y)*f.width+x)*4;
                const int red=f.rgba[offset],green=f.rgba[offset+1],blue=f.rgba[offset+2];
                unsigned colors=0;
                if (f.rgba[offset+3]>=251 && red>=low_[0] && red<=high_[0] && green>=low_[1] && green<=high_[1] && blue>=low_[2] && blue<=high_[2]) {
                    for (std::size_t i=0;i<N;++i) {
                        const auto rgb=palette_[i];
                        if (std::abs(red-int(rgb>>16))<=tolerance_ && std::abs(green-int((rgb>>8)&255))<=tolerance_
                            && std::abs(blue-int(rgb&255))<=tolerance_) colors|=1u<<i;
                    }
                }
                if constexpr (TexturedGold) {
                    const bool gold=SpawnGold
                        ? red>=80 && green*100>=red*80 && green*100<=red*94 && blue*100>=red*12 && blue*100<=red*50
                        : red>=90 && green*100>=red*60 && green*100<=red*78 && blue*100<=red*40 && (green-blue)*100>=red*30;
                    if (f.rgba[offset+3]>=251 && gold) colors|=1u<<N;
                }
                if (colors) for (std::size_t i=0;i<count;++i) {
                    const auto bit=1u<<i;
                    if (!(colors&bit)) continue;
                    runs[i]=(previous&bit)?runs[i]+1:1;
                    const int patch_width=i==N?gold_width:width,patch_height=i==N?gold_height:height;
                    if (runs[i]<patch_width) continue;
                    auto& column=columns[static_cast<std::size_t>(x)*count+i];
                    column.height=column.row==y-1?column.height+1:1; column.row=y;
                    if (column.height>=patch_height) {
                        if constexpr (SpawnGold) if (i==N) gold_columns[x]=true;
                        ++matches[i]; auto& box=bounds[i];
                        box.left=std::min(box.left,x-patch_width+1); box.top=std::min(box.top,y-patch_height+1);
                        box.right=std::max(box.right,x+1); box.bottom=std::max(box.bottom,y+1);
                    }
                }
                previous=colors;
            }
        }
        if constexpr (TexturedGold && !SpawnGold) {
            const auto& box=bounds[N];
            if (box.right-box.left<f.width*.5 || box.bottom-box.top<f.height*.08
                || box.left>f.width*.35 || box.right<f.width*.65
                || matches[N]<static_cast<std::size_t>(f.width)*(bottom-top)*.12) matches[N]=0;
        }
        if constexpr (TexturedGold) matches[N]/=4;
        if constexpr (SpawnGold) {
            unsigned panels=0;
            int run=0,gap=gold_width;
            for (const bool gold : gold_columns) {
                if (gold) {
                    if (++run==gold_width && gap>=width) ++panels;
                } else {
                    gap=run?1:gap+1; run=0;
                }
            }
            if (panels<3) matches[N]=0;
        }
        const auto best=static_cast<std::size_t>(std::max_element(matches.begin(),matches.end())-matches.begin());
        if (matches[best]>=required) {
            const auto& box=bounds[best];
            result->region.knowledge=Knowledge::present;
            result->region.value={double(box.left)/f.width,double(box.top)/f.height,
                double(box.right-box.left)/f.width,double(box.bottom-box.top)/f.height};
            reading.knowledge=Knowledge::present; reading.value=true;
        } else {
            if constexpr (TexturedGold && !SpawnGold) {
                if (const auto honeycomb=honeycomb_region(f,scale,scale_y)) {
                    result->region.knowledge=Knowledge::present; result->region.value=*honeycomb;
                    reading.knowledge=Knowledge::present; reading.value=true;
                    return result;
                }
            }
            reading.knowledge=Knowledge::absent; result->region.knowledge=Knowledge::absent;
        }
        return result;
    }
};
class HiveReference final : public LandmarkSource {
    std::shared_ptr<const DetectionSource> pixels_;
    std::shared_ptr<const LandmarkSource> proximity_;
    mutable std::mutex mutex_;
    mutable Observation<bool> confirmed_;
    mutable ObservationContext context_;
    mutable Time processed_{Time(-1)},armed_until_{Time(-1)};
public:
    HiveReference(std::shared_ptr<const DetectionSource> pixels,std::shared_ptr<const LandmarkSource> proximity)
        :pixels_(std::move(pixels)),proximity_(std::move(proximity)) {}
    Observation<bool> read(const ObservationContext& c) const override {
        std::lock_guard lock(mutex_);
        auto unknown=[&] { confirmed_={}; return Observation<bool>{}; };
        if (c.generation!=context_.generation || c.window.id!=context_.window.id || c.window.process!=context_.window.process
            || c.window.bounds!=context_.window.bounds) { unknown(); processed_=armed_until_=Time(-1); }
        context_=c;
        const auto proximity=proximity_->read(c);
        if (proximity.has_value(c,Time(2000)) && proximity.value) armed_until_=c.now+Time(30000);
        const auto packet=pixels_->read();
        if (c.now>armed_until_ || !packet || !packet->error.empty()) return unknown();
        const auto& context=packet->context;
        const Observation<bool> envelope{Knowledge::present,true,context.now,context.window,context.generation};
        if (!envelope.has_value(c,Time(2000))) return unknown();
        const auto* detected=dynamic_cast<const HiveViewDetection*>(packet->value.get());
        if (!detected) return unknown();
        const auto it=detected->readings.find("hive.backboard");
        if (it==detected->readings.end()) return unknown();
        const auto& visible=it->second;
        if (!visible.usable(c,Time(2000)) || visible.captured!=context.now || context.now<processed_) return unknown();
        const auto stamp=visible.captured;
        if (visible.knowledge==Knowledge::absent || !visible.value
            || (detected->facing.has_value(c,Time(2000)) && !detected->facing.value)) {
            unknown(); processed_=context.now;
            return {Knowledge::absent,false,stamp,c.window,c.generation};
        }
        if (!detected->facing.has_value(c,Time(2000)) || detected->facing.captured!=context.now) return unknown();
        const auto& region=detected->region;
        if (!region.has_value(c,Time(2000)) || region.captured!=context.now || !region.value.valid()
            || region.value.x<0 || region.value.y<0 || region.value.x+region.value.width>1.000001
            || region.value.y+region.value.height>1.000001) return unknown();
        if (context.now==processed_) return confirmed_.has_value(c,Time(2000))?confirmed_:Observation<bool>{};
        processed_=context.now;
        confirmed_={Knowledge::present,true,stamp,c.window,c.generation};
        return confirmed_;
    }
};
}
std::shared_ptr<const Detector> hive_view_detector() { return std::make_shared<FlatView<10,true>>(hive_palette,"hive.backboard",3,4); }
std::shared_ptr<const Detector> spawn_view_detector() { return std::make_shared<FlatView<6,true,true>>(spawn_palette,"spawn.reference",0,5); }
std::shared_ptr<const LandmarkSource> hive_reference_source(std::shared_ptr<const DetectionSource> pixels,
    std::shared_ptr<const LandmarkSource> proximity) {
    if (!pixels || !proximity) throw std::invalid_argument("Hive reference requires pixel and proximity observations");
    return std::make_shared<HiveReference>(std::move(pixels),std::move(proximity));
}
}
