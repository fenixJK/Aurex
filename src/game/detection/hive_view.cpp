#include "hive_view.hpp"
#include <algorithm>
#include <array>
#include <mutex>
#include <opencv2/imgproc.hpp>

namespace hive::game {
namespace {
constexpr std::array<std::uint32_t,6> spawn_palette{
    0x937b1f,0x796615,0xc1ae55,0x957a70,0x988a55,0xc3bca5
};
std::optional<Rect> centered_honeycomb(const Frame& f) {
    // Sample only the search region into a small mask; no full-frame resize or DPI dependency.
    const double shrink=640./std::max(f.width,f.height);
    cv::Mat gold(std::max(1,static_cast<int>(std::lround(f.height*shrink))),
        std::max(1,static_cast<int>(std::lround(f.width*shrink))),CV_8U,cv::Scalar(0));
    const double scale_x=double(f.width)/gold.cols,scale_y=double(f.height)/gold.rows;
    const std::size_t stride=std::size_t(f.width)*4;
    for (int y=gold.rows*.15;y<gold.rows*.65;++y) {
        const double sy=(y+.5)*scale_y-.5,dy=sy-std::floor(sy);
        const auto* row=f.rgba.data()+static_cast<std::size_t>(sy)*stride;
        for (int x=gold.cols*.2;x<gold.cols*.8;++x) {
            const double sx=(x+.5)*scale_x-.5,dx=sx-std::floor(sx);
            const auto* pixel=row+static_cast<std::size_t>(sx)*4;
            const auto channel=[&](int c) { return static_cast<int>((1-dy)*((1-dx)*pixel[c]+dx*pixel[c+4])
                +dy*((1-dx)*pixel[stride+c]+dx*pixel[stride+c+4])+.5); };
            const int red=channel(0),green=channel(1),blue=channel(2);
            gold.at<std::uint8_t>(y,x)=channel(3)>=251 && red>=120 && green*100>=red*63
                && green*100<=red*105 && blue*100<=red*70 ? 255 : 0;
        }
    }
    // Bridge small lettering/highlight gaps in the cell rims before finding their interiors.
    cv::morphologyEx(gold,gold,cv::MORPH_CLOSE,cv::getStructuringElement(cv::MORPH_RECT,{5,5}));
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(gold,contours,hierarchy,cv::RETR_CCOMP,cv::CHAIN_APPROX_SIMPLE);
    std::vector<cv::Rect> cells;
    for (std::size_t i=0;i<contours.size();++i) {
        if (hierarchy[i][3]<0) continue;
        const auto box=cv::boundingRect(contours[i]);
        const double x=box.x+box.width*.5,y=box.y+box.height*.5;
        if (box.width<8 || box.height<8 || box.width>140 || box.height>140
            || y>gold.rows*.5) continue;
        const double aspect=double(box.width)/box.height,fill=cv::contourArea(contours[i])/box.area();
        if (aspect<.9 || aspect>1.6 || fill<.55 || fill>.85) continue;
        std::vector<cv::Point> hull,polygon;
        cv::convexHull(contours[i],hull);
        cv::approxPolyDP(hull,polygon,cv::arcLength(hull,true)*.04,true);
        if (polygon.size()<5 || polygon.size()>7) continue;
        // A number or highlight can merge/split a corner; compare the whole outline too.
        std::vector<cv::Point2f> normalized;
        for (const auto point : hull)
            normalized.push_back({float(point.x-box.x)/(box.width-1),float(point.y-box.y)/(box.height-1)});
        static const std::vector<cv::Point2f> hexagon{{0,.5},{.25,0},{.75,0},{1,.5},{.75,1},{.25,1}};
        if (cv::matchShapes(normalized,hexagon,cv::CONTOURS_MATCH_I1,0)>.12) continue;
        // A centered cell and a similar neighbor survive tool occlusion without accepting a hive only at the screen edge.
        for (const auto& other : cells) {
            const double other_x=other.x+other.width*.5;
            if ((x<gold.cols*.44 || x>gold.cols*.56)
                && (other_x<gold.cols*.44 || other_x>gold.cols*.56)) continue;
            const int height=std::max(box.height,other.height);
            const double dx=std::abs(x-other_x),dy=std::abs(y-other.y-other.height*.5);
            if (std::min(box.height,other.height)*2<height || dy<std::min(box.height,other.height)*.5
                || dy>height*4 || dx>std::max(box.width,other.width)*1.5) continue;
            const auto region=box|other;
            return Rect{double(region.x)/gold.cols,double(region.y)/gold.rows,
                double(region.width)/gold.cols,double(region.height)/gold.rows};
        }
        cells.push_back(box);
    }
    return {};
}
class HiveView final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& f=input.frame;
        if (!f.valid() || !input.generation || (input.cancelled && input.cancelled())) return {};
        auto result=std::make_shared<HiveViewDetection>();
        auto& reading=result->readings["hive.backboard"];
        reading=result->facing={Knowledge::unknown,false,f.captured,f.window,input.generation};
        result->region={Knowledge::unknown,{},f.captured,f.window,input.generation};
        const double scale=f.width/f.window.bounds.width,scale_y=f.height/f.window.bounds.height;
        if (scale<.5 || scale>4 || scale_y<.5 || scale_y>4 || std::abs(scale-scale_y)>.1
            || f.width>8192 || f.height>8192 || f.window.bounds.width<320 || f.window.bounds.height<200) return result;
        const auto region=centered_honeycomb(f);
        if (input.cancelled && input.cancelled()) return {};
        reading=result->facing={region?Knowledge::present:Knowledge::absent,bool(region),f.captured,f.window,input.generation};
        result->region={reading.knowledge,region.value_or(Rect{}),f.captured,f.window,input.generation};
        return result;
    }
};
template<std::size_t N>
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
        const int width=std::max(1,static_cast<int>(std::lround(22*scale)));
        const int height=std::max(1,static_cast<int>(std::lround(4*scale_y)));
        const int top=f.height*quarter_/4, bottom=f.height*(quarter_+1)/4;
        const std::size_t required=std::max<std::size_t>(1,static_cast<std::size_t>(f.width)*f.width/3200);
        struct Column { int row{-2},height{}; };
        constexpr std::size_t count=N+1;
        const int gold_width=std::max(1,static_cast<int>(std::lround(64*scale)));
        const int gold_height=std::max(1,static_cast<int>(std::lround(16*scale_y)));
        std::vector<Column> columns(static_cast<std::size_t>(f.width)*count);
        std::array<std::size_t,count> matches{};
        std::vector<bool> gold_columns(f.width);
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
                {
                    const bool gold=red>=80 && green*100>=red*80 && green*100<=red*94
                        && blue*100>=red*12 && blue*100<=red*50;
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
                        if (i==N) gold_columns[x]=true;
                        ++matches[i]; auto& box=bounds[i];
                        box.left=std::min(box.left,x-patch_width+1); box.top=std::min(box.top,y-patch_height+1);
                        box.right=std::max(box.right,x+1); box.bottom=std::max(box.bottom,y+1);
                    }
                }
                previous=colors;
            }
        }
        matches[N]/=4;
        {
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
std::shared_ptr<const Detector> hive_view_detector() { return std::make_shared<HiveView>(); }
std::shared_ptr<const Detector> spawn_view_detector() { return std::make_shared<FlatView<6>>(spawn_palette,"spawn.reference",0,5); }
std::shared_ptr<const LandmarkSource> hive_reference_source(std::shared_ptr<const DetectionSource> pixels,
    std::shared_ptr<const LandmarkSource> proximity) {
    if (!pixels || !proximity) throw std::invalid_argument("Hive reference requires pixel and proximity observations");
    return std::make_shared<HiveReference>(std::move(pixels),std::move(proximity));
}
}
