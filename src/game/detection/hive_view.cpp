#include "hive_view.hpp"
#include <algorithm>
#include <array>
#include <mutex>
#include <opencv2/imgproc.hpp>

namespace hive::game {
namespace {
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
        // A frontal flat-top hexagon is wider than tall. Near-square holes
        // also occur in bee faces and in a hive viewed obliquely.
        if (aspect<1.1 || aspect>1.6 || fill<.55 || fill>.85) continue;
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
class SpawnView final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& f=input.frame;
        if (!f.valid() || !input.generation || (input.cancelled && input.cancelled())) return {};
        auto result=std::make_shared<HiveViewDetection>();
        auto& reading=result->readings["spawn.reference"];
        reading={Knowledge::absent,false,f.captured,f.window,input.generation};
        result->region={Knowledge::absent,{},f.captured,f.window,input.generation};
        // A hive-row view has several white platforms beneath honeycomb boards.
        // Normalize scene geometry by image size, independently of desktop DPI.
        const double dpi_x=f.width/f.window.bounds.width,dpi_y=f.height/f.window.bounds.height;
        if (dpi_x<.5 || dpi_x>4 || dpi_y<.5 || dpi_y>4 || std::abs(dpi_x-dpi_y)>.1
            || f.width>8192 || f.height>8192 || f.window.bounds.width<320 || f.window.bounds.height<200) {
            reading.knowledge=result->region.knowledge=Knowledge::unknown; return result;
        }
        cv::Mat rgba(f.height,f.width,CV_8UC4,const_cast<std::uint8_t*>(f.rgba.data())),small;
        const double scale=640./std::max(f.width,f.height);
        cv::resize(rgba,small,{},scale,scale,cv::INTER_AREA);
        cv::Mat white(small.rows,small.cols,CV_8U,cv::Scalar(0));
        for (int y=small.rows*.2;y<small.rows*.6;++y) for (int x=0;x<small.cols;++x) {
            const auto p=small.at<cv::Vec4b>(y,x);
            if (p[3]>=251 && std::min({p[0],p[1],p[2]})>=185
                && std::max({p[0],p[1],p[2]})-std::min({p[0],p[1],p[2]})<40) white.at<std::uint8_t>(y,x)=255;
        }
        cv::morphologyEx(white,white,cv::MORPH_CLOSE,cv::getStructuringElement(cv::MORPH_RECT,{9,3}));
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(white,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
        std::vector<std::pair<cv::Rect,bool>> platforms;
        for (const auto& contour:contours) {
            const auto box=cv::boundingRect(contour);
            if (box.width<small.cols*.04 || box.width>small.cols*.3 || box.height<3
                || box.y+box.height>small.rows*.45
                || box.width<box.height*2.5 || box.width>box.height*16 || cv::contourArea(contour)<box.area()*.4) continue;
            // Red claim arrows end on the white pads of unowned hives. They
            // distinguish unclaimed spawn from a close view of an owned hive.
            unsigned red=0;
            for (int y=box.y;y<box.y+box.height;++y) for (int x=box.x;x<box.x+box.width;++x) {
                const auto p=small.at<cv::Vec4b>(y,x);
                red+=p[0]>150 && p[0]>p[1]*1.6 && p[0]>p[2]*1.6;
            }
            unsigned gold=0,total=0;
            for (int y=std::max(0,box.y-int(small.rows*.18));y<box.y;++y) for (int x=box.x;x<box.x+box.width;++x) {
                const auto p=small.at<cv::Vec4b>(y,x); ++total;
                gold+=p[0]>=80 && p[1]>=p[0]*.6 && p[1]<=p[0]*1.05 && p[2]<=p[0]*.7;
            }
            if (gold*4<total) continue;
            // The red arrow must terminate below an empty golden backboard;
            // a red tool in front of a leaderboard is not a claim arrow.
            const bool available=red && gold>=total*.65;
            for (const auto& [other,other_red]:platforms) {
                const double a=box.x+box.width*.5,b=other.x+other.width*.5;
                if ((!available && !other_red) || std::min(a,b)>small.cols*.45 || std::max(a,b)<small.cols*.55
                    || std::abs(box.y-other.y)>small.rows*.06) continue;
                const auto row=box|other;
                reading.knowledge=Knowledge::present; reading.value=true;
                result->region={Knowledge::present,{double(row.x)/small.cols,double(row.y)/small.rows,
                    double(row.width)/small.cols,double(row.height)/small.rows},f.captured,f.window,input.generation};
                return result;
            }
            platforms.emplace_back(box,available);
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
std::shared_ptr<const Detector> spawn_view_detector() { return std::make_shared<SpawnView>(); }
std::shared_ptr<const LandmarkSource> hive_reference_source(std::shared_ptr<const DetectionSource> pixels,
    std::shared_ptr<const LandmarkSource> proximity) {
    if (!pixels || !proximity) throw std::invalid_argument("Hive reference requires pixel and proximity observations");
    return std::make_shared<HiveReference>(std::move(pixels),std::move(proximity));
}
}
