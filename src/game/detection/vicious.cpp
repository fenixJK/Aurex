#include "vicious.hpp"
#include "vicious_glyphs.hpp"
#include "vicious_masks.hpp"
#include "bar_regions.hpp"
#include "perception.hpp"
#include <algorithm>
#include <opencv2/imgproc.hpp>

namespace hive::game {
namespace {
enum class Ink { light,red,dark,gold };
cv::Mat ink(const cv::Mat& rgba,Ink mode) {
    cv::Mat mask(rgba.rows,rgba.cols,CV_8UC1);
    for (int y=0;y<rgba.rows;++y) for (int x=0;x<rgba.cols;++x) {
        const auto p=rgba.at<cv::Vec4b>(y,x); const int r=p[0],g=p[1],b=p[2];
        const auto lo=std::min({r,g,b}),hi=std::max({r,g,b});
        mask.at<unsigned char>(y,x)=(mode==Ink::light?lo>=140 && hi-lo<=70:
            mode==Ink::red?r>80 && r>g*1.5 && r>b*1.4:
            mode==Ink::gold?r>=125 && g>=105 && r>b+35 && g>b+25:hi<135)?255:0;
    }
    return mask;
}
// Compare complete fixed words across font scales. Color thresholds tolerate
// antialiasing/compression; the word shapes, rather than single pixels, identify the event.
bool label_matches(const cv::Mat& mask,const cv::Mat& label,double low,double high,double threshold=.72) {
    if (mask.empty() || cv::countNonZero(mask)<20) return false;
    cv::Mat softened;cv::GaussianBlur(mask,softened,{3,3},.6);
    for (double scale=low;scale<=high+.001;scale+=.1) {
        cv::Mat pattern,score;
        cv::resize(label,pattern,{},scale,scale,cv::INTER_AREA);
        if (pattern.rows>mask.rows || pattern.cols>mask.cols || pattern.rows<6) continue;
        cv::GaussianBlur(pattern,pattern,{3,3},.6);
        cv::matchTemplate(softened,pattern,score,cv::TM_CCOEFF_NORMED);
        double best{};cv::minMaxLoc(score,nullptr,&best);
        if (best>=threshold) return true;
    }
    return false;
}
std::vector<cv::Mat> words(const cv::Mat& mask) {
    if (mask.empty() || !cv::countNonZero(mask)) return {};
    const auto text=mask(cv::boundingRect(mask));
    std::vector<cv::Mat> result;
    for (int x=0;x<text.cols;) {
        if (!cv::countNonZero(text.col(x))) { ++x; continue; }
        const int begin=x++; int end=x;
        while (x<text.cols && x-end<std::max(2.,text.rows*.3)) {
            if (cv::countNonZero(text.col(x))) end=x+1;
            ++x;
        }
        const auto word=text.colRange(begin,end);
        result.push_back(word(cv::boundingRect(word)).clone());
    }
    return result;
}
float defeat_score(const cv::Mat& mask) {
    // Normalize each complete word independently. Resizing a whole sentence
    // lets small font/spacing differences accumulate over its entire width.
    static const auto phrase=words(detail::vicious_toast());
    if (mask.empty() || !cv::countNonZero(mask)) return 0.f;
    const auto text=mask(cv::boundingRect(mask));
    std::vector<std::pair<int,int>> spans;
    for (int x=0;x<text.cols;) {
        if (!cv::countNonZero(text.col(x))) { ++x;continue; }
        const int begin=x++;
        while (x<text.cols && cv::countNonZero(text.col(x))) ++x;
        spans.emplace_back(begin,x);
    }
    if (spans.size()>120) return 0.f;
    // Font hinting can make an inter-letter gap as wide as a space. Try
    // bounded adjacent spans instead of assuming one fixed space width.
    std::vector<float> scores(spans.size()+1,0.f);
    for (std::size_t k=0;k<phrase.size();++k) {
        const auto& expected=phrase[k];
        std::vector<float> next(spans.size()+1,-1.f);
        const double widest=(double(expected.cols)/expected.rows+.12)*text.rows+3;
        for (std::size_t begin=0;begin<spans.size();++begin) {
            if (scores[begin]<0) continue;
            for (std::size_t end=begin;end<spans.size();++end) {
                if (spans[end].second-spans[begin].first>widest) break;
                auto word=text.colRange(spans[begin].first,spans[end].second);
                word=word(cv::boundingRect(word));
                if (word.rows<6) cv::resize(word,word,{},2,2,cv::INTER_NEAREST);
                auto score=detail::similarity(word,expected,.12);
                // The final verb distinguishes a defeat from other announcements.
                // Small font hinting needs its own shapes to keep this check strict.
                if (k==4) for (const auto& label:detail::vicious_defeated())
                    score=std::max(score,detail::similarity(word,label,.12));
                const float minimum=k==4?.88f:(k==2 || k==3)?.65f:.68f;
                if (score>=minimum) next[end+1]=std::max(next[end+1],scores[begin]+score);
            }
        }
        scores=std::move(next);
    }
    return *std::max_element(scores.begin(),scores.end())/phrase.size();
}
bool defeat_matches(const cv::Mat& mask,bool chat=false) {
    const auto score=defeat_score(mask);
    if (score>=.80f) return true;
    if (!chat || mask.empty() || !cv::countNonZero(mask)) return false;
    // Chat uses small italic text; remove its slant before comparing the
    // same five words. Scenery crops do not use this extra pass.
    const auto text=mask(cv::boundingRect(mask));
    for (double slant:{.16,.22,.28}) {
        cv::Mat upright;
        const cv::Mat transform=(cv::Mat_<double>(2,3)<<1,slant,0,0,1,0);
        cv::warpAffine(text,upright,transform,{text.cols+int(std::ceil(text.rows*slant)),text.rows},cv::INTER_LINEAR);
        cv::threshold(upright,upright,127,255,cv::THRESH_BINARY);
        if (defeat_score(upright)>=.80f) return true;
    }
    return false;
}
struct LocalEvidence { bool visible{},toast{}; };
LocalEvidence local_evidence(const Frame& frame) {
    // Only inspect the play area and notification stack. No OCR or other HUD decoding.
    const cv::Mat full(frame.height,frame.width,CV_8UC4,const_cast<std::uint8_t*>(frame.rgba.data()));
    Frame small{frame.window,frame.captured,frame.width,frame.height,{},frame.region};
    if (frame.width>1280) {
        cv::Mat scaled;cv::resize(full,scaled,{1280,int(std::lround(frame.height*1280./frame.width))},0,0,cv::INTER_AREA);
        small.width=scaled.cols;small.height=scaled.rows;small.rgba.assign(scaled.data,scaled.data+scaled.total()*4);
    } else small.rgba=frame.rgba;
    const auto world=small.crop({.05,.15,.9,.7});
    const cv::Mat area(world.height,world.width,CV_8UC4,const_cast<std::uint8_t*>(world.rgba.data()));
    auto bars=detail::bar_regions(world,[](const std::uint8_t* p) {
        return p[0]>65 && p[0]<200 && p[1]<85 && p[2]<100
            && p[1]<p[0]*.65 && p[2]<p[0]*.7;
    },true);
    const auto healthy=detail::bar_regions(world,[](const std::uint8_t* p) {
        return p[1]>220 && p[0]<90 && p[2]<90;
    },true);
    bars.insert(bars.end(),healthy.begin(),healthy.end());
    LocalEvidence result;
    for (const auto& bar:bars) {
        const int x=std::max(0,int(bar.x-bar.width*.4)),y=std::max(0,int(bar.y-bar.height*1.5));
        const int right=std::min(world.width,int(bar.x+bar.width*1.2)),bottom=int(bar.y);
        if (bottom<=y || right<=x || bar.width<35 || bar.height<6) continue;
        const auto text=ink(area(cv::Rect(x,y,right-x,bottom-y)),Ink::light);
        const auto scale=bar.height/20.;
        if (label_matches(text,detail::vicious_name(),std::max(.6,scale*.7),std::min(2.,scale*1.3),.70)) {
            result.visible=true; break;
        }
    }
    // UI text has a fixed size in window points. Scaling the entire scene to
    // 1280 made notices progressively smaller on wide/Retina windows.
    const cv::Rect stack(int(frame.width*.55),int(frame.height*.5),frame.width-int(frame.width*.55),frame.height-int(frame.height*.5));
    cv::Mat toast;
    const double ui_scale=frame.width/(frame.region.width*frame.window.bounds.width);
    cv::resize(full(stack),toast,{},1/ui_scale,1/ui_scale,cv::INTER_AREA);
    // A defeat notice has a gold background; blue rewards and death notices do not qualify.
    cv::Mat gold(toast.rows,toast.cols,CV_8UC1);
    for(int y=0;y<toast.rows;++y)for(int x=0;x<toast.cols;++x) {
        const auto p=toast.at<cv::Vec4b>(y,x);gold.at<unsigned char>(y,x)=p[0]>120 && p[1]>100 && p[2]<p[1]*.65?255:0;
    }
    cv::morphologyEx(gold,gold,cv::MORPH_CLOSE,cv::getStructuringElement(cv::MORPH_RECT,{7,3}));
    std::vector<std::vector<cv::Point>> contours;cv::findContours(gold,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    for(const auto& contour:contours) {
        const auto box=cv::boundingRect(contour);
        if(box.width<100 || box.height<9 || box.height>45 || box.width<box.height*6)continue;
        if(defeat_matches(ink(toast(box),Ink::dark))) {result.toast=true;break;}
    }
    return result;
}
std::array<unsigned,2> notifications(const DetectionInput& input) {
    const auto& f=input.frame;
    const double scale=f.width/(f.region.width*f.window.bounds.width);
    const double full_width=f.width/f.region.width,full_height=f.height/f.region.height;
    const double width=std::floor(std::min(475.,f.window.bounds.width/2.5))*scale;
    const double height=std::floor(std::min(334.,f.window.bounds.height/3.464))*scale;
    const int left=std::max(0,int(std::lround(full_width-8*scale-width-f.region.x*full_width)));
    const int top=std::max(0,int(std::lround(40*scale-f.region.y*full_height)));
    const int right=std::min(f.width,int(std::lround(full_width-8*scale-f.region.x*full_width)));
    const int bottom=std::min(f.height,top+int(std::lround(height)));
    if (right<=left || bottom<=top) return {};
    const cv::Mat pixels(f.height,f.width,CV_8UC4,const_cast<std::uint8_t*>(f.rgba.data()));
    cv::Mat chat;
    // Work in window points: Retina capture does not quadruple the search cost.
    cv::resize(pixels(cv::Rect(left,top,right-left,bottom-top)),chat,
        cv::Size(std::max(1,int(std::lround((right-left)/scale))),std::max(1,int(std::lround((bottom-top)/scale)))),0,0,cv::INTER_AREA);
    std::array<std::vector<int>,2> rows;
    for (const auto& glyph:detail::vicious_glyphs) {
        const auto near=[](const cv::Vec4b& pixel,std::uint32_t rgb) {
            return pixel[3]>=240 && std::abs(int(pixel[0])-int(rgb>>16))<=5
                && std::abs(int(pixel[1])-int((rgb>>8)&255))<=5 && std::abs(int(pixel[2])-int(rgb&255))<=5;
        };
        for (int y=0;y+glyph.height<=chat.rows;++y) {
            if (input.cancelled && input.cancelled()) return {};
            for (int x=0;x+glyph.width<=chat.cols;++x) {
                if (!near(chat.at<cv::Vec4b>(y,x),glyph.rgb[0])) continue;
                bool match=true;
                for (int dy=0;match && dy<glyph.height;++dy) for (int dx=0;dx<glyph.width;++dx)
                    if (!near(chat.at<cv::Vec4b>(y+dy,x+dx),glyph.rgb[dy*glyph.width+dx])) { match=false; break; }
                if (match) rows[glyph.defeated].push_back(y);
            }
        }
    }
    std::array<unsigned,2> counts{};
    for (unsigned k=0;k<2;++k) {
        auto& hits=rows[k]; std::sort(hits.begin(),hits.end());
        int previous=-100;
        for (const auto y:hits) if (y-previous>12) { ++counts[k]; previous=y; }
    }
    for (unsigned kind=0;kind<2;++kind) {
        const auto text=ink(chat,kind?Ink::gold:Ink::red);
        cv::Mat lines;cv::morphologyEx(text,lines,cv::MORPH_CLOSE,cv::getStructuringElement(cv::MORPH_RECT,{kind?16:6,2}));
        std::vector<std::vector<cv::Point>> contours;cv::findContours(lines,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
        unsigned announcements=0;
        for(const auto& contour:contours) {
            auto box=cv::boundingRect(contour);
            if(box.width<60 || box.height<5 || box.height>30 || box.width<box.height*6)continue;
            box=(box+cv::Size(4,4)-cv::Point(2,2))&cv::Rect(0,0,chat.cols,chat.rows);
            if(kind?defeat_matches(text(box),true):label_matches(text(box),detail::vicious_attack(),.6,1.8))++announcements;
        }
        counts[kind]=std::max(counts[kind],announcements);
    }
    return counts;
}
class ViciousDetector final : public Detector {
    mutable Observation<ViciousSignals> previous_;
    mutable std::array<unsigned,2> counts_{},candidate_{};
    mutable Time candidate_at_{};
    mutable LocalEvidence local_{};
    mutable bool toast_seen_{};
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        auto result=std::make_shared<ViciousDetection>();
        const auto& f=input.frame;
        if (!f.valid() || f.width>16000 || f.height>16000) return result;
        const ObservationContext context{f.window,input.generation,f.captured};
        const auto counts=notifications(input);
        const auto local=local_evidence(f);
        if (input.cancelled && input.cancelled()) return result;
        // A capture pause may require a new row baseline, but must not erase
        // a confirmed result before the hunt consumes it. Window/generation
        // changes still discard all events from the previous server.
        ViciousSignals events=previous_.has_value(context,Time::max())?previous_.value:ViciousSignals{};
        if (!previous_.has_value(context,Time(5000))) {
            // Joining a server or opening this detector must not replay old chat.
            counts_=candidate_=counts; candidate_at_=f.captured;
            local_={}; toast_seen_=local.toast;
        } else {
            if (candidate_!=counts) { candidate_=counts; candidate_at_=f.captured; }
            else if (f.captured>candidate_at_) {
                // Require distinct frames, and count a notification once as it arrives.
                if (counts[0]>counts_[0] && !events.engaged) events.engaged=candidate_at_;
                if (counts[1]>counts_[1] && !events.defeated) events.defeated=candidate_at_;
                counts_=counts;
            }
        }
        // A nameplate is current local evidence even if activation happened before
        // entering the field. A toast must arrive after baseline, on distinct frames.
        events.visible=local.visible && local_.visible && f.captured>previous_.captured;
        events.world_visible=game_hud_visible(f,input.generation);
        events.candidates=events.world_visible?vicious_candidates(f):std::vector<ViciousCandidate>{};
        if(local.toast && local_.toast && !toast_seen_ && f.captured>previous_.captured) {
            if(!events.defeated) events.defeated=f.captured;
            toast_seen_=true;
        }
        if(!local.toast && !local_.toast) toast_seen_=false;
        local_=local;
        for (auto* event:{&events.engaged,&events.defeated})
            if (*event && f.captured-**event>std::chrono::minutes(3)) event->reset();
        result->events={Knowledge::present,events,f.captured,f.window,input.generation};
        previous_=result->events;
        return result;
    }
};
}
std::shared_ptr<const Detector> vicious_detector() { return std::make_shared<ViciousDetector>(); }
Observation<ViciousSignals> read_vicious(const DetectionSource& source,const ObservationContext& context) {
    const auto packet=source.read();
    if (!packet || !packet->error.empty()) return {};
    const Observation<bool> envelope{Knowledge::present,true,packet->context.now,packet->context.window,packet->context.generation};
    if (!envelope.has_value(context,Time(2000))) return {};
    const auto result=std::dynamic_pointer_cast<const ViciousDetection>(packet->value);
    return result && result->events.has_value(context,Time(2000))?result->events:Observation<ViciousSignals>{};
}
}
