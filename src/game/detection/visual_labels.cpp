#include "visual_labels.hpp"
#include "fixed_labels.hpp"
#include "result_labels.hpp"
#include "mask_match.hpp"
#include <algorithm>
#include <cctype>

namespace hive::game {
namespace {
std::vector<cv::Rect> text_rows(const cv::Mat& ink,double maximum_height) {
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(ink,contours,hierarchy,cv::RETR_CCOMP,cv::CHAIN_APPROX_SIMPLE);
    std::vector<cv::Rect> glyphs;
    for (std::size_t i=0;i<contours.size();++i) {
        if (hierarchy[i][3]>=0) continue; // Letter holes aren't separate glyphs.
        const auto r=cv::boundingRect(contours[i]);
        if (r.height>=5 && r.height<=maximum_height && r.width<=r.height*2 && r.width>=1)
            glyphs.push_back(r);
    }
    std::sort(glyphs.begin(),glyphs.end(),[](const auto& a,const auto& b) { return a.x<b.x; });
    struct Row { cv::Rect bounds; int glyph_height; };
    std::vector<Row> grouped;
    for (const auto& glyph:glyphs) {
        auto row=std::find_if(grouped.begin(),grouped.end(),[&](const auto& line) {
            const auto& r=line.bounds;
            return std::min(r.y+r.height,glyph.y+glyph.height)-std::max(r.y,glyph.y)>=std::min(r.height,glyph.height)*.5
                && glyph.height>=line.glyph_height*.55 && line.glyph_height>=glyph.height*.55
                && glyph.x<=r.x+r.width+std::max(r.height,glyph.height)*2;
        });
        if (row==grouped.end()) grouped.push_back({glyph,glyph.height});
        else { row->bounds|=glyph; row->glyph_height=std::max(row->glyph_height,glyph.height); }
    }
    std::vector<cv::Rect> rows;
    for (const auto& row:grouped) rows.push_back(row.bounds);
    return rows;
}
}
static std::vector<TextLine> read_labels(const Frame& frame,Rect region,std::span<const std::string_view> labels,LabelInk ink_mode,bool whole_rows) {
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
        if (!light.empty()) light.at<unsigned char>(y,x)=lo>=(whole_rows?140:160) && hi-lo<=(whole_rows?70:55)?255:0;
        // Dark blue UI lettering has colored antialiased edges on bright buttons.
        if (!dark.empty()) dark.at<unsigned char>(y,x)=hi<=140 && hi-lo<=80?255:0;
    }
    static const auto vocabulary=[] {
        std::vector<detail::FixedLabel> result(detail::fixed_labels.begin(),detail::fixed_labels.end());
        result.insert(result.end(),detail::result_labels.begin(),detail::result_labels.end());
        return result;
    }();
    static const auto patterns=[] {
        std::vector<cv::Mat> result;
        for (const auto& label:vocabulary) result.push_back(detail::reference(label.width,label.height,label.bits));
        return result;
    }();
    std::vector<std::size_t> selected;
    for (std::size_t k=0;k<vocabulary.size();++k)
        if (std::find(labels.begin(),labels.end(),vocabulary[k].text)!=labels.end()) selected.push_back(k);
    const bool numeric_rows=whole_rows && std::any_of(selected.begin(),selected.end(),[&](auto k) { return vocabulary[k].numeric_prefix; });
    std::vector<TextLine> found;
    for (const auto& ink:{light,dark}) {
        if (ink.empty()) continue;
        std::vector<cv::Rect> rows;
        if (!whole_rows) rows=text_rows(ink,frame.height*.08);
        if (whole_rows) {
            for (int y=0;y<ink.rows;) {
                if (!cv::countNonZero(ink.row(y))) { ++y;continue; }
                const int start=y++;
                while (y<ink.rows && cv::countNonZero(ink.row(y))) ++y;
                auto box=cv::boundingRect(ink.rowRange(start,y));box.y+=start;
                if (box.height>=5) rows.push_back(box);
            }
        }
        for (const auto& row:rows) {
            if (row.width<row.height*1.4) continue;
            {
                // Match complete labels before splitting at glyph gaps. Small fonts
                // can make a single word look like several separate spans.
                TextLine best{"",{double(left+row.x),double(top+row.y),double(row.width),double(row.height)},0.f};
                // Segment the row once; all requested words share these candidates.
                std::vector<cv::Rect> suffixes{row};
                for (int x=1;numeric_rows && x<row.width;++x) {
                    if (cv::countNonZero(ink({row.x+x-1,row.y,1,row.height})) || !cv::countNonZero(ink({row.x+x,row.y,1,row.height}))) continue;
                    const cv::Rect area(row.x+x,row.y,row.width-x,row.height);
                    const auto tight=cv::boundingRect(ink(area));
                    suffixes.push_back({area.x+tight.x,area.y+tight.y,tight.width,tight.height});
                }
                for (const auto k:selected) {
                    const auto& label=vocabulary[k];
                    for (std::size_t i=0;i<suffixes.size();++i) {
                        if (label.numeric_prefix?(i==0 || suffixes[i].x-row.x>row.height*5.5 || suffixes[i].x-row.x<row.height*1.4):i!=0) continue;
                        const auto score=detail::similarity(ink(suffixes[i]),patterns[k],.08);
                        if (score<.86f || score<=best.confidence.value_or(0)) continue;
                        best.text=label.text;best.confidence=score;
                    }
                }
                if (whole_rows || !best.text.empty()) {
                    found.push_back(std::move(best));
                    continue;
                }
            }
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
                    const auto& label=vocabulary[k];
                    if (label.numeric_prefix) continue;
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
    const auto alternatives=found;
    std::erase_if(found,[&](const auto& item) {
        return std::any_of(alternatives.begin(),alternatives.end(),[&](const auto& other) {
            const auto a=item.pixels,b=other.pixels;
            return item.text!=other.text && std::abs(a.x-b.x)<2 && std::abs(a.y-b.y)<2
                && std::abs(a.width-b.width)<2 && std::abs(a.height-b.height)<2
                && other.confidence.value_or(0)>item.confidence.value_or(0);
        });
    });
    return found;
}
std::vector<TextLine> fixed_ui_labels(const Frame& frame,Rect region,std::span<const std::string_view> labels,LabelInk ink) {
    return read_labels(frame,region,labels,ink,false);
}
std::vector<TextLine> fixed_ui_labels(const Frame& frame,const FixedLabelQuery& query,std::span<const TextLine> anchors) {
    auto region=query.region;
    if (!query.anchor.empty()) {
        const TextLine* anchor=nullptr;
        for (const auto& line:anchors) if (line.text==query.anchor) { if (anchor) return {};anchor=&line; }
        if (!anchor) return {};
        const auto box=anchor->pixels;
        region={(box.x+region.x*box.width)/frame.width,(box.y+region.y*box.width)/frame.height,
            region.width*box.width/frame.width,region.height*box.width/frame.height};
    }
    const std::vector<std::string_view> names(query.labels.begin(),query.labels.end());
    return read_labels(frame,region,names,query.ink,query.rows);
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
