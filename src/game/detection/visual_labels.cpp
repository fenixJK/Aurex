#include "visual_labels.hpp"
#include "fixed_labels.hpp"
#include "mask_match.hpp"
#include <algorithm>
#include <cctype>

namespace hive::game {
namespace {
std::vector<cv::Rect> text_rows(const cv::Mat& ink,double maximum_height) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(ink,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    std::vector<cv::Rect> glyphs;
    for (const auto& contour:contours) {
        const auto r=cv::boundingRect(contour);
        if (r.height>=5 && r.height<=maximum_height && r.width<=r.height*2 && r.width>=1)
            glyphs.push_back(r);
    }
    std::sort(glyphs.begin(),glyphs.end(),[](const auto& a,const auto& b) { return a.x<b.x; });
    std::vector<cv::Rect> rows;
    for (const auto& glyph:glyphs) {
        auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& r) {
            return std::min(r.y+r.height,glyph.y+glyph.height)-std::max(r.y,glyph.y)>=std::min(r.height,glyph.height)*.5
                && glyph.height>=r.height*.55 && r.height>=glyph.height*.55
                && glyph.x<=r.x+r.width+std::max(r.height,glyph.height)*2;
        });
        if (row==rows.end()) rows.push_back(glyph); else *row|=glyph;
    }
    return rows;
}
}
std::vector<TextLine> fixed_ui_labels(const Frame& frame,Rect region,std::span<const std::string_view> labels,LabelInk ink_mode) {
    if (!frame.valid() || labels.empty() || !region.valid() || region.x<0 || region.y<0
        || region.x+region.width>1 || region.y+region.height>1) return {};
    const int left=int(region.x*frame.width),top=int(region.y*frame.height);
    const int width=std::min(frame.width-left,int(std::ceil(region.width*frame.width)));
    const int height=std::min(frame.height-top,int(std::ceil(region.height*frame.height)));
    cv::Mat light,dark;
    if (ink_mode!=LabelInk::dark) light=cv::Mat(height,width,CV_8UC1,cv::Scalar(0));
    if (ink_mode!=LabelInk::light) dark=cv::Mat(height,width,CV_8UC1,cv::Scalar(0));
    for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
        const auto i=(std::size_t(top+y)*frame.width+left+x)*4;
        const int r=frame.rgba[i],g=frame.rgba[i+1],b=frame.rgba[i+2];
        const auto lo=std::min({r,g,b}),hi=std::max({r,g,b});
        if (!light.empty()) light.at<unsigned char>(y,x)=lo>=160 && hi-lo<=55?255:0;
        // Dark blue UI lettering has colored antialiased edges on bright buttons.
        if (!dark.empty()) dark.at<unsigned char>(y,x)=hi<=140 && hi-lo<=80?255:0;
    }
    static const auto patterns=[] {
        std::vector<cv::Mat> result;
        for (const auto& label:detail::fixed_labels) result.push_back(detail::reference(label.width,label.height,label.bits));
        return result;
    }();
    std::vector<std::size_t> selected;
    for (std::size_t k=0;k<detail::fixed_labels.size();++k)
        if (std::find(labels.begin(),labels.end(),detail::fixed_labels[k].text)!=labels.end()) selected.push_back(k);
    std::vector<TextLine> found;
    for (const auto& ink:{light,dark}) {
        if (ink.empty()) continue;
        const auto rows=text_rows(ink,frame.height*.08);
        for (const auto& row:rows) {
            if (row.width<row.height*1.4) continue;
            // Candidate words remain bounded: a short label cannot match inside a longer word.
            const auto line=ink(row); std::vector<std::pair<int,int>> words;
            for (int x=0;x<line.cols;) {
                if (!cv::countNonZero(line.col(x))) { ++x; continue; }
                const int begin=x++;int end=x;
                while (x<line.cols && x-end<std::max(2.,row.height*.36)) {
                    if (cv::countNonZero(line.col(x))) end=x+1;
                    ++x;
                }
                words.emplace_back(begin,end);
            }
            for (std::size_t i=0;i<words.size();++i) for (std::size_t j=i;j<words.size() && j<i+4;++j) {
                cv::Rect crop(row.x+words[i].first,row.y,words[j].second-words[i].first,row.height);
                const auto tight=cv::boundingRect(ink(crop));crop={crop.x+tight.x,crop.y+tight.y,tight.width,tight.height};
                for (const auto k:selected) {
                    const auto& label=detail::fixed_labels[k];
                    const auto score=detail::similarity(ink(crop),patterns[k],.08);
                    if (score<.86f) continue;
                    const Rect box{double(left+crop.x),double(top+crop.y),double(crop.width),double(crop.height)};
                    auto prior=std::find_if(found.begin(),found.end(),[&](const auto& other) {
                        return other.text==label.text && std::abs(other.pixels.x-box.x)<crop.height*.5
                            && std::abs(other.pixels.y-box.y)<crop.height*.5;
                    });
                    if (prior==found.end()) found.push_back({std::string(label.text),box,score});
                    else if (score>prior->confidence.value_or(0)) *prior={std::string(label.text),box,score};
                }
            }
        }
    }
    // Prefer a whole control label over a word contained in it (e.g. Don't Respawn).
    const auto candidates=found;
    std::erase_if(found,[&](const auto& item) {
        return std::any_of(candidates.begin(),candidates.end(),[&](const auto& whole) {
            const auto a=item.pixels,b=whole.pixels;
            return item.text!=whole.text && b.width>a.width && a.x>=b.x-1 && a.y>=b.y-1
                && a.x+a.width<=b.x+b.width+1 && a.y+a.height<=b.y+b.height+1;
        });
    });
    return found;
}
Reading<TextSnapshot> with_fixed_labels(const ObservationContext& c,const Reading<TextSnapshot>& text,
    const Reading<TextSnapshot>& labels,std::span<const std::string_view> names) {
    if (!text.has_value(c,Time(2000)) || !labels.has_value(c,Time(2000))
        || text.value.width!=labels.value.width || text.value.height!=labels.value.height) return {};
    auto result=text; result.captured=std::min(text.captured,labels.captured);
    auto normalized=[](std::string_view value) {
        std::string out;
        for (unsigned char ch:value) if (std::isalnum(ch)) out+=char(std::tolower(ch));
        return out;
    };
    std::erase_if(result.value.lines,[&](const auto& line) {
        return std::any_of(names.begin(),names.end(),[&](const auto& name) {return normalized(line.text)==normalized(name);});
    });
    for (const auto& line:labels.value.lines)
        if (std::find(names.begin(),names.end(),line.text)!=names.end()) result.value.lines.push_back(line);
    return result;
}

}
