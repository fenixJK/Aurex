#include "blooms.hpp"
#include "game/detection/vision/chroma.hpp"
#include "menus.hpp"

namespace hive::game {
namespace {
class BloomDetector final : public Detector {
public:
    std::vector<Rect> text_regions(const Frame&,std::uint64_t) const override { return {}; }
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        auto result=std::make_shared<Blooms>();
        if (!input.frame.valid() || (input.cancelled && input.cancelled())) return result;
        const auto& frame=input.frame;
        cv::Mat rgba(frame.height,frame.width,CV_8UC4,const_cast<std::uint8_t*>(frame.rgba.data()));
        cv::Mat scaled=rgba,scene;
        const double scale=std::min(1.,1080./frame.height);
        if (scale<1) cv::resize(rgba,scaled,{},scale,scale,cv::INTER_AREA);
        cv::cvtColor(scaled,scene,cv::COLOR_RGBA2BGR);
        scaled.release();
        const int inset=std::lround(scene.rows*.12);
        if (scene.rows-inset*2<20) return result;
        const auto crop=scene(cv::Rect(0,inset,scene.cols,scene.rows-inset*2));
        cv::Scalar mean,deviation;
        cv::meanStdDev(crop,mean,deviation);
        result->visible=std::max({deviation[0],deviation[1],deviation[2]})>=12 && std::max({mean[0],mean[1],mean[2]})>=20;
        if (!result->visible) return result;
        const double size=scene.rows/1080.;
        vision::ColorPatternConfig config;
        config.centerColor.hues=vision::HueRangeSet({{16,32}});
        config.centerColor.satRange={50,150}; config.centerColor.valRange={85,255};
        config.centerMorph={std::max(1,int(std::lround(2*size))),std::max(1,int(std::lround(3*size))),1};
        config.shape={std::max(20,int(std::lround(250*size*size))),std::max(800,int(std::lround(8000*size*size))),.80f,.43f};
        config.matchBlooms=true; config.context.enabled=true;
        config.context.innerRadiusPercent=105; config.context.outerRadiusPercent=200;
        config.context.supportColor.hues=vision::HueRangeSet({{0,179}});
        config.context.supportColor.satRange={0,255}; config.context.supportColor.valRange={60,255};
        config.context.excludeHues=vision::HueRangeSet({{52,68},{24,48}});
        config.context.excludeSatRange={0,255}; config.context.excludeValRange={60,255};
        config.context.minSupportRatio=.42f;
        const auto found=vision::ColorPatternFinder(config).Find(crop,false);
        if (input.cancelled && input.cancelled()) return std::make_shared<Blooms>();
        if (std::none_of(found.detections.begin(),found.detections.end(),[](const auto& flower) { return flower.metrics.accepted; })) return result;
        cv::Mat whole_hsv; cv::cvtColor(scene,whole_hsv,cv::COLOR_BGR2HSV);
        const auto hsv=whole_hsv(cv::Rect(0,inset,scene.cols,scene.rows-inset*2));
        std::vector<Rect> overlays;
        const auto menus=menu_controls(input);
        if (!menus.empty()) {
            Rect bar=menus.front().bounds;
            for (const auto& menu:menus) {
                const auto right=std::max(bar.x+bar.width,menu.bounds.x+menu.bounds.width);
                const auto bottom=std::max(bar.y+bar.height,menu.bounds.y+menu.bounds.height);
                bar.x=std::min(bar.x,menu.bounds.x); bar.y=std::min(bar.y,menu.bounds.y);
                bar.width=right-bar.x; bar.height=bottom-bar.y;
            }
            overlays.push_back({0,0,std::max(bar.x+bar.width*1.5,menus.front().bounds.width*8),bar.y+bar.height*1.2});
        }
        cv::Mat neutral;
        cv::inRange(whole_hsv,cv::Scalar(0,0,115),cv::Scalar(179,55,255),neutral);
        std::vector<std::vector<cv::Point>> borders;
        cv::findContours(neutral,borders,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
        std::vector<cv::Rect> slots;
        for (const auto& border:borders) {
            const auto box=cv::boundingRect(border);
            if (box.y<scene.rows*.65 || box.height<scene.rows*.035 || box.height>scene.rows*.15
                || box.width<box.height*.7 || box.width>box.height*1.4 || cv::contourArea(border)<box.area()*.55) continue;
            slots.push_back(box);
        }
        cv::Rect hotbar;
        for (const auto& slot:slots) {
            cv::Rect row=slot; unsigned count=0;
            for (const auto& other:slots)
                if (std::abs(other.y-slot.y)<slot.height*.15 && std::abs(other.height-slot.height)<slot.height*.2) { row|=other; ++count; }
            if (count>=4 && row.area()>hotbar.area()) hotbar=row;
        }
        if (hotbar.area()>0) overlays.push_back({double(hotbar.x-hotbar.height)/scene.cols,(hotbar.y-hotbar.height*.75)/scene.rows,
            double(hotbar.width+hotbar.height*2)/scene.cols,hotbar.height*1.75/scene.rows});
        for (const auto& flower:found.detections) {
            if (!flower.metrics.accepted) continue;
            const auto p=flower.centerPx;
            const int radius=std::max(1,int(flower.radiusPx*.65));
            const cv::Rect inner_box(std::max(0,p.x-radius),std::max(0,p.y-radius),std::min(hsv.cols-std::max(0,p.x-radius),radius*2+1),std::min(hsv.rows-std::max(0,p.y-radius),radius*2+1));
            cv::Mat disk(inner_box.size(),CV_8UC1,cv::Scalar(0));
            cv::circle(disk,{p.x-inner_box.x,p.y-inner_box.y},radius,cv::Scalar(255),-1);
            cv::Scalar center_mean,center_deviation; cv::meanStdDev(hsv(inner_box),center_mean,center_deviation,disk);
            if (center_deviation[2]>12 || center_deviation[1]>20) continue;
            Bloom bloom{{double(p.x)/scene.cols,double(p.y+inset)/scene.rows},flower.radiusPx/scene.rows,{}};
            if (bloom.center.x<.08 || bloom.center.x>.92) continue;
            if (std::any_of(overlays.begin(),overlays.end(),[&](const auto& box) {
                return bloom.center.x>=box.x && bloom.center.x<=box.x+box.width && bloom.center.y>=box.y && bloom.center.y<=box.y+box.height;
            })) continue;
            const double inner=flower.radiusPx*1.15,outer=flower.radiusPx*2;
            unsigned total=0;
            for (int y=std::max(0,int(p.y-outer));y<std::min(hsv.rows,int(p.y+outer+1));++y)
                for (int x=std::max(0,int(p.x-outer));x<std::min(hsv.cols,int(p.x+outer+1));++x) {
                    const double distance=(x-p.x)*(x-p.x)+(y-p.y)*(y-p.y);
                    if (distance<inner*inner || distance>outer*outer) continue;
                    ++total; const auto pixel=hsv.at<cv::Vec3b>(y,x);
                    const int h=pixel[0],s=pixel[1],v=pixel[2];
                    bloom.colors[1]+=s<=40 && v>=165;
                    bloom.colors[2]+=h>=80 && h<104 && s>=70 && v>=90;
                    bloom.colors[3]+=h>=35 && h<=79 && s>=90 && v>=60;
                    bloom.colors[4]+=h>=135 && h<=172 && s>=55 && v>=90;
                    bloom.colors[5]+=h>=104 && h<=128 && s>=90 && v>=45;
                    bloom.colors[6]+=(h<=9 || h>=173) && s>=90 && v>=50;
                }
            for (auto& value:bloom.colors) value/=std::max(1u,total);
            result->flowers.push_back(bloom);
        }
        return result;
    }
};
}
std::shared_ptr<const Detector> bloom_detector() { return std::make_shared<BloomDetector>(); }
Observation<Blooms> read_blooms(const DetectionSource& source,const ObservationContext& context) {
    const auto result=source.read();
    if (!result || !result->error.empty()) return {};
    const auto value=std::dynamic_pointer_cast<const Blooms>(result->value);
    if (!value || !value->visible) return {};
    Observation<Blooms> reading{Knowledge::present,*value,result->context.now,result->context.window,result->context.generation};
    return reading.has_value(context,Time(1000))?reading:Observation<Blooms>{};
}
}
