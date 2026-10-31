#include "honey.hpp"
#include "hud_numbers.hpp"
#include <cmath>
#include <map>
#include <opencv2/imgproc.hpp>
namespace hive::game {
using namespace detail;
namespace {
std::optional<std::uint64_t> honey_amount(std::string text,bool isolated=false) {
    text=normalize(text);
    if (text.starts_with("honey ")) text.erase(0,6);
    // OCR may join the total, its smaller (+.../sec) caption and even the
    // adjacent Pollen label. Only the leading balance belongs to this counter.
    auto end=text.find_first_of("(+");
    if (const auto pollen=text.find(" pollen");pollen!=std::string::npos) end=std::min(end,pollen);
    if (end!=std::string::npos) text.resize(end);
    text.erase(text.find_last_not_of(' ')+1);
    if (isolated) {
        // The gold digit crop excludes labels and rate captions. Repair common
        // Windows OCR substitutions here only, then require agreement between
        // independently prepared views. Separators carry no numeric meaning.
        std::string digits;
        for (char c:text) {
            if (c=='o') c='0';
            else if (c=='i' || c=='l' || c=='|') c='1';
            else if (c=='a') c='4';
            if (c>='0' && c<='9') digits+=c;
            else if (c!=',' && c!='.' && c!=' ') return {};
        }
        return amount(digits,1000000000000000000ULL);
    }
    std::replace(text.begin(),text.end(),'.',',');
    for (std::size_t i=0;i<text.size();) {
        if (text[i]==' ' && ((i && text[i-1]==',') || (i+1<text.size() && text[i+1]==','))) text.erase(i,1);
        else ++i;
    }
    // Some engines read thousands separators as spaces. Keep the same strict
    // three-digit grouping validation rather than concatenating arbitrary digits.
    std::replace(text.begin(),text.end(),' ',',');
    return amount(text,1000000000000000000ULL);
}
Reading<std::uint64_t> honey_reading(const Frame& frame,std::span<const TextLine> lines,std::uint64_t generation,
    const std::optional<std::array<Rect,2>>& resources,bool isolated=false) {
    Reading<std::uint64_t> result{Knowledge::unknown,{},frame.captured,frame.window,generation};
    if (!frame.valid() || !resources) return result;
    const auto& region=resources->front();
    std::optional<std::uint64_t> found;
    for (const auto& line:lines) {
        const auto& r=line.pixels; const auto text=normalize(line.text);
        if (!inside(r,frame) || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.5f || *line.confidence>1))) continue;
        const auto x=(r.x-region.x)/region.width,y=r.y+r.height/2;
        // The paired HUD boxes establish which counter this is even when OCR
        // misses the small Honey label. The rate caption starts farther right.
        if (x<0 || x>=.5 || y<region.y || y>region.y+region.height || (!text.starts_with("honey ") && x<.15)) continue;
        if (const auto value=honey_amount(text,isolated)) {
            if (found && *found!=*value) return result;
            found=value;
        }
    }
    if (found) { result.value=*found; result.knowledge=Knowledge::present; }
    return result;
}
std::optional<cv::Rect> honey_digits(const Frame& frame,const Rect& r) {
    // Find the first gold text run inside the balance box. Its following gap
    // separates the smaller /sec caption, whose digits must never join the total.
    // The paired HUD supplies the scale and position, including windowed games.
    const int x0=int(std::floor(r.x+r.width*.19)),y0=int(std::floor(r.y+r.height*.25));
    const cv::Rect inner(x0,y0,int(std::floor(r.x+r.width*.98))-x0,int(std::ceil(r.y+r.height*.8))-y0);
    const cv::Mat source(frame.height,frame.width,CV_8UC4,const_cast<std::uint8_t*>(frame.rgba.data()));
    cv::Mat ink(inner.height,inner.width,CV_8UC1,cv::Scalar(0));
    for (int y=0;y<inner.height;++y) for (int x=0;x<inner.width;++x) {
        const auto c=source.at<cv::Vec4b>(y+inner.y,x+inner.x);
        if (c[0]>=150 && c[1]>=125 && c[0]-c[2]>=20 && c[1]-c[2]>=8) ink.at<unsigned char>(y,x)=255;
    }
    int first=0; while (first<ink.cols && !cv::countNonZero(ink.col(first))) ++first;
    if (first>=ink.cols*.15) return {};
    int end=first; while (end<ink.cols && cv::countNonZero(ink.col(end))) ++end;
    const int glyph_height=cv::boundingRect(ink.colRange(first,end)).height;
    if (glyph_height<3) return {};
    int last=end,gap=0;
    for (;end<ink.cols;++end) {
        if (cv::countNonZero(ink.col(end))) { last=end+1; gap=0; }
        else if (++gap>=std::max(3,int(std::ceil(glyph_height*.75)))) break;
    }
    const auto text=cv::boundingRect(ink.colRange(first,last));
    const int margin=std::max(1,glyph_height/4);
    const int left=std::max(inner.x+first+text.x-margin,inner.x),top=int(std::floor(r.y));
    return cv::Rect(left,top,std::min(inner.x+first+text.x+text.width+margin,int(r.x+r.width))-left,
        int(std::ceil(r.y+r.height))-top);
}
Reading<std::uint64_t> recognize_honey(const DetectionInput& input,const std::array<Rect,2>& resources) {
    const auto& frame=input.frame;
    Reading<std::uint64_t> result{Knowledge::unknown,{},frame.captured,frame.window,input.generation};
    const auto digits=honey_digits(frame,resources.front());
    if (!digits || (input.cancelled && input.cancelled())) return result;
    const auto area=*digits;
    const cv::Mat source(frame.height,frame.width,CV_8UC4,const_cast<std::uint8_t*>(frame.rgba.data()));
    const auto crop=source(area);
    struct View { cv::Rect box; double sx,sy; };
    std::map<std::uint64_t,unsigned> votes;
    const auto consensus=[&]() -> std::optional<std::uint64_t> {
        unsigned best=1;std::optional<std::uint64_t> value;
        for (const auto& [candidate,count]:votes) {
            if (count>best) { best=count;value=candidate; }
            else if (count==best) value.reset();
        }
        return value;
    };
    // Keep the inexpensive three-view pass. If Windows cannot read it, use
    // additional aspect ratios/contrast from this same capture, not another
    // capture or Natro's fifty separate OCR calls.
    for (int pass=0;pass<2;++pass) {
        if (input.cancelled && input.cancelled()) return result;
        std::vector<View> views;
        std::vector<cv::Mat> images;
        constexpr int padding=16;
        int width=0,height=padding;
        for (int i=0;i<(pass?4:3);++i) {
            const int h=pass?56+i*24:96+i*32;
            const double aspect=pass?std::array{.7,1.3,.85,1.15}[i]:.9+i*.1;
            const int w=int(std::round(area.width*double(h)/area.height*aspect));
            cv::Mat image;
            cv::resize(crop,image,{w,h},0,0,cv::INTER_CUBIC);
            if (pass || i) {
                cv::Mat gray; cv::cvtColor(image,gray,cv::COLOR_RGBA2GRAY);
                // Separate the light digits from the gray bar and dark outline.
                gray.convertTo(gray,CV_8U,i%2?1.8:2.4,i%2?-120:-210);
                cv::bitwise_not(gray,gray); cv::cvtColor(gray,image,cv::COLOR_GRAY2RGBA);
            }
            views.push_back({{padding,height,w,h},double(w)/area.width,double(h)/area.height});
            images.push_back(std::move(image)); width=std::max(width,w+2*padding); height+=h+padding;
        }
        Frame prepared{frame.window,frame.captured,width,height,{}};
        prepared.rgba.assign(std::size_t(width)*height*4,255);
        cv::Mat canvas(height,width,CV_8UC4,prepared.rgba.data());
        for (std::size_t i=0;i<views.size();++i) images[i].copyTo(canvas(views[i].box));
        // One small OCR job per pass, independent of other modules' text regions.
        const auto lines=input.recognize(prepared);
        if (lines.size()>128 || (input.cancelled && input.cancelled())) return result;
        for (const auto& view:views) {
            std::vector<TextLine> mapped;
            for (auto line:lines) {
                const auto b=line.pixels;
                if (!inside(b,prepared)) continue;
                // OCR boxes include a little blank margin around their lettering.
                // Attribute a line to its view without discarding edge digits.
                const double x=std::max(b.x,double(view.box.x)),y=std::max(b.y,double(view.box.y));
                const double w=std::min(b.x+b.width,double(view.box.x+view.box.width))-x,
                    h=std::min(b.y+b.height,double(view.box.y+view.box.height))-y;
                if (w<=0 || h<=0 || w*h<b.width*b.height*.8) continue;
                line.pixels={area.x+(x-view.box.x)/view.sx,area.y+(y-view.box.y)/view.sy,w/view.sx,h/view.sy};
                mapped.push_back(std::move(line));
            }
            const auto reading=honey_reading(frame,mapped,input.generation,resources,true);
            if (reading.knowledge==Knowledge::present) ++votes[reading.value];
        }
        if (const auto value=consensus()) {
            result.value=*value;result.knowledge=Knowledge::present;return result;
        }
    }
    return result;
}
class HoneyDetector final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        auto result=std::make_shared<HoneyDetection>();
        const auto resources=resource_hud_regions(input);
        result->amount=resources && input.recognize?recognize_honey(input,*resources):
            honey_reading(input.frame,input.text,input.generation,resources);
        return result;
    }
};
}
Reading<std::uint64_t> read_honey(const Frame& frame,const std::vector<TextLine>& lines,std::uint64_t generation) {
    return honey_reading(frame,lines,generation,resource_hud_regions(frame));
}
std::shared_ptr<const Detector> honey_detector() { return std::make_shared<HoneyDetector>(); }
}
