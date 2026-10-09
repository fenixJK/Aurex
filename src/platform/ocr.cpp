#include "ocr.hpp"
#include <algorithm>
#include <cstring>
#include <cctype>
#include <tuple>

namespace hive {
namespace {
struct Segment { int start,size; double begin,end; };
std::vector<Segment> segments(int size,int maximum) {
    if (size<=maximum) return {{0,size,0,double(size)}};
    std::vector<int> starts{0};
    while (starts.back()+maximum<size) {
        if (starts.size()>=64) throw std::invalid_argument("OCR image needs too many crops");
        starts.push_back(std::min(starts.back()+maximum/2,size-maximum));
    }
    std::vector<Segment> result;
    for (std::size_t i=0;i<starts.size();++i) result.push_back({starts[i],maximum,
        i?(starts[i-1]+maximum+starts[i])/2.0:0,
        i+1<starts.size()?(starts[i]+maximum+starts[i+1])/2.0:double(size)});
    return result;
}
std::string text_key(const std::string& text) {
    std::string result; bool space=false;
    for (unsigned char c : text) {
        if (std::isspace(c)) { space=!result.empty(); continue; }
        if (space) result+=' ';
        result+=static_cast<char>(std::tolower(c)); space=false;
    }
    return result;
}
bool same_reading(const TextLine& a,const TextLine& b) {
    const auto x=std::max(a.pixels.x,b.pixels.x),y=std::max(a.pixels.y,b.pixels.y);
    const auto width=std::min(a.pixels.x+a.pixels.width,b.pixels.x+b.pixels.width)-x;
    const auto height=std::min(a.pixels.y+a.pixels.height,b.pixels.y+b.pixels.height)-y;
    const auto area=a.pixels.width*a.pixels.height+b.pixels.width*b.pixels.height;
    return width>0 && height>0 && width*height/(area-width*height)>=0.7 && text_key(a.text)==text_key(b.text);
}
void check_cancel(const std::function<bool()>& cancelled) {
    if (cancelled && cancelled()) throw std::runtime_error("OCR cancelled");
}
}
std::vector<TextLine> split_ocr_columns(const TextLine& line,std::span<const TextLine> words) {
    std::vector<TextLine> result;
    Rect previous{};
    for (const auto& word:words) {
        const auto& box=word.pixels;
        if (result.empty() || box.x-previous.x-previous.width>2*std::max(box.height,previous.height)) result.push_back(word);
        else {
            auto& group=result.back(); const auto bounds=group.pixels;
            group.text+=' '; group.text+=word.text;
            group.pixels={std::min(bounds.x,box.x),std::min(bounds.y,box.y),
                std::max(bounds.x+bounds.width,box.x+box.width)-std::min(bounds.x,box.x),
                std::max(bounds.y+bounds.height,box.y+box.height)-std::min(bounds.y,box.y)};
        }
        previous=box;
    }
    return result.size()>1?result:std::vector<TextLine>{line};
}
std::vector<TextLine> recognize_tiled(const OcrImage& image,int maximum,const OcrReader& reader,const std::function<bool()>& cancelled) {
    if (image.width<=0 || image.height<=0 || image.width>32768 || image.height>32768 || maximum<32 || maximum>32768 || !reader
        || image.rgba.size()!=static_cast<std::size_t>(image.width)*image.height*4) throw std::invalid_argument("Invalid OCR image or engine limit");
    check_cancel(cancelled);
    const auto columns=segments(image.width,maximum),rows=segments(image.height,maximum);
    if (columns.size()*rows.size()>64) throw std::invalid_argument("OCR image needs too many crops");
    std::vector<TextLine> result; std::vector<std::uint8_t> buffer;
    for (const auto& row : rows) for (const auto& column : columns) {
        check_cancel(cancelled);
        OcrImage crop{column.size,row.size,image.rgba};
        if (columns.size()>1 || rows.size()>1) {
            buffer.resize(static_cast<std::size_t>(crop.width)*crop.height*4);
            for (int y=0;y<crop.height;++y) std::memcpy(buffer.data()+static_cast<std::size_t>(y)*crop.width*4,
                image.rgba.data()+(static_cast<std::size_t>(row.start+y)*image.width+column.start)*4,static_cast<std::size_t>(crop.width)*4);
            crop.rgba=buffer;
        }
        auto lines=reader(crop); check_cancel(cancelled);
        if (lines.size()>4096) throw std::runtime_error("OCR returned too many text lines");
        for (auto& line : lines) {
            auto& box=line.pixels;
            if (!box.valid() || box.x<0 || box.y<0 || box.x+box.width>crop.width || box.y+box.height>crop.height
                || line.text.empty() || line.text.size()>4096 || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<0 || *line.confidence>1))) continue;
            if ((column.start && box.x<2) || (row.start && box.y<2)
                || (column.start+column.size<image.width && box.x+box.width>crop.width-2)
                || (row.start+row.size<image.height && box.y+box.height>crop.height-2)) continue;
            box.x+=column.start; box.y+=row.start;
            const double x=box.x+box.width/2,y=box.y+box.height/2;
            if (x<column.begin || x>=column.end || y<row.begin || y>=row.end) continue;
            if (columns.size()>1 || rows.size()>1) {
                const auto duplicate=std::find_if(result.begin(),result.end(),[&](const auto& other) { return same_reading(line,other); });
                if (duplicate!=result.end()) {
                    if (line.confidence.value_or(-1)>duplicate->confidence.value_or(-1)) *duplicate=std::move(line);
                    continue;
                }
            }
            if (result.size()>=2048) throw std::runtime_error("OCR returned too many text lines");
            result.push_back(std::move(line));
        }
    }
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b) { return std::tie(a.pixels.y,a.pixels.x)<std::tie(b.pixels.y,b.pixels.x); });
    return result;
}
}
