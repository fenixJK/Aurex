#include "conversation.hpp"
#include "visual_labels.hpp"
#include <algorithm>
#include <array>
#include <opencv2/imgproc.hpp>

namespace hive::game {
namespace {
// The continue caption can be only a few pixels high on a small game window.
// Recognize its enclosing NPC panel as well: a bright blue frame, darker name
// strip and inset body, and a separate caption in the bottom-right corner.
// A blue rectangle or white text alone is not a conversation control.
std::vector<ConversationView> panels(const Frame& f) {
    const double scale=std::min(1.,800./f.width);
    cv::Mat pixels(f.height,f.width,CV_8UC4,const_cast<unsigned char*>(f.rgba.data())),small;
    cv::resize(pixels,small,{std::max(1,int(std::lround(f.width*scale))),std::max(1,int(std::lround(f.height*scale)))},0,0,cv::INTER_AREA);
    cv::Mat blue(small.rows,small.cols,CV_8UC1,cv::Scalar(0)),bright=blue.clone(),dark=blue.clone(),letters=blue.clone();
    for (int y=small.rows/2;y<small.rows;++y) for (int x=0;x<small.cols;++x) {
        const auto c=small.at<cv::Vec4b>(y,x);
        const int r=c[0],g=c[1],b=c[2];
        if (b>=120 && b>r*1.4 && b>g*1.2) {
            blue.at<unsigned char>(y,x)=255;
            if (b>=215) bright.at<unsigned char>(y,x)=255;
            else dark.at<unsigned char>(y,x)=255;
        }
        if (r>=110 && g>=130 && b>=150 && g>=r-25 && b-r<=140)
            letters.at<unsigned char>(y,x)=255;
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(dark,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    std::vector<ConversationView> found;
    for (const auto& contour:contours) {
        const auto box=cv::boundingRect(contour);
        if (box.width<small.cols*.23 || box.width>small.cols*.68 || box.height<small.rows*.06 || box.height>small.rows*.25
            || box.width<box.height*2.8 || box.width>box.height*7 || box.y<small.rows*.55
            || std::abs(box.x+box.width/2.-small.cols/2.)>small.cols*.08
            || cv::contourArea(contour)<box.area()*.92) continue;
        const auto region=[&](double x,double y,double w,double h) {
            return cv::Rect(box.x+int(x*box.width),box.y+int(y*box.height),std::max(1,int(w*box.width)),std::max(1,int(h*box.height)));
        };
        const auto fraction=[&](const cv::Mat& mask,const cv::Rect& area) {
            if ((area&cv::Rect(0,0,small.cols,small.rows))!=area) return 0.;
            return double(cv::countNonZero(mask(area)))/area.area();
        };
        const auto name=region(.02,-.46,.22,.32),footer=region(.73,1.07,.25,.2);
        if (fraction(blue,box)<.85
            || fraction(dark,name)<.55 || fraction(letters,name)<.025
            || fraction(bright,region(.6,-.46,.32,.32))<.85
            || fraction(bright,region(.05,-.08,.9,.05))<.85
            || fraction(bright,region(-.02,.08,.015,.8))<.8
            || fraction(bright,region(1.01,.08,.015,.8))<.8
            || fraction(bright,region(.08,1.1,.5,.17))<.85) continue;
        ConversationView panel{{},true};
        if (fraction(letters,box)>=.005 && fraction(letters,footer)>=.035 && fraction(letters,footer)<=.4) {
            const auto text=cv::boundingRect(letters(footer));
            if (text.width>=footer.width*.4 && text.height>=2 && text.height<=footer.height*.9)
                panel.next=Point{(footer.x+text.x+text.width/2.)/small.cols,(footer.y+text.y+text.height/2.)/small.rows};
        }
        found.push_back(panel);
    }
    return found;
}
class ConversationDetector final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& f=input.frame;
        if (!f.valid() || (input.cancelled && input.cancelled())) return {};
        auto result=std::make_shared<ConversationDetection>();
        const auto visible=panels(f);
        if (!visible.empty()) {
            // Keep a typing/transitioning dialogue open without guessing a click.
            result->view=visible.size()==1?visible.front():ConversationView{{},true};
            return result;
        }
        static constexpr std::array<std::string_view,1> labels{"Click to continue"};
        const auto rows=fixed_ui_labels(f,{.05,.5,.9,.5},labels);
        const TextLine* footer=nullptr;
        for (const auto& line:rows) {
            if (input.cancelled && input.cancelled()) return {};
            const auto r=line.pixels;
            if (!r.valid() || r.x<0 || r.x+r.width>f.width || r.y<f.height*.5 || r.y+r.height>f.height
                || r.height<6 || line.text.size()>80) continue;
            if (line.text!="Click to continue") continue;
            if (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.8f || *line.confidence>1)) return result;
            if (footer) return result;
            footer=&line;
        }
        if (!footer) return result;
        const auto r=footer->pixels;
        const auto blue=[&](int x,int y) {
            if (x<0 || x>=f.width || y<0 || y>=f.height) return false;
            const auto i=(static_cast<std::size_t>(y)*f.width+x)*4;
            return f.rgba[i+2]>=100 && f.rgba[i+2]>f.rgba[i]*1.3 && f.rgba[i+2]>f.rgba[i+1]*1.15;
        };
        int baseline=0;
        for (int y=int(r.y+r.height+1);y<std::min(f.height,int(r.y+r.height*2));++y) {
            const int middle=int(r.x+r.width/2);
            if (!blue(middle,y)) continue;
            int a=middle,b=middle;
            while (a>0 && blue(a-1,y)) --a;
            while (b<f.width && blue(b,y)) ++b;
            if (b-a<r.height*15 || b-a>f.width*.95 || a>r.x || b<r.x+r.width) continue;
            baseline=y; break;
        }
        if (!baseline) return result;
        result->view.visible=true;
        result->view.next=Point{(r.x+r.width/2)/f.width,(r.y+r.height/2)/f.height};
        return result;
    }
};
class Source final : public ConversationSource {
    std::shared_ptr<const DetectionSource> source_;
public:
    explicit Source(std::shared_ptr<const DetectionSource> source):source_(std::move(source)) {}
    Observation<ConversationView> read(const ObservationContext& c) const override {
        const auto result=source_->read(); if (!result) return {};
        if (!result->error.empty()) throw std::runtime_error(result->error);
        const auto* value=dynamic_cast<const ConversationDetection*>(result->value.get());
        if (!value) return {};
        Observation<ConversationView> reading{Knowledge::present,value->view,result->context.now,result->context.window,result->context.generation};
        return reading.has_value(c,Time(2000))?reading:Observation<ConversationView>{};
    }
};
}
std::shared_ptr<const Detector> conversation_detector() { return std::make_shared<ConversationDetector>(); }
std::shared_ptr<const ConversationSource> conversation_source(std::shared_ptr<const DetectionSource> source) {
    if (!source) throw std::invalid_argument("Conversation needs a detection source");
    return std::make_shared<Source>(std::move(source));
}
}
