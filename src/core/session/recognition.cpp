#include "recognition.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace hive::game {
std::vector<Rect> exclude_text_region(std::vector<Rect> regions,Rect excluded) {
    std::vector<Rect> result;
    for (const auto& r:regions) {
        const auto left=std::max(r.x,excluded.x),right=std::min(r.x+r.width,excluded.x+excluded.width);
        const auto top=std::max(r.y,excluded.y),bottom=std::min(r.y+r.height,excluded.y+excluded.height);
        if (left>=right || top>=bottom) { result.push_back(r); continue; }
        for (const auto part:std::array{Rect{r.x,r.y,r.width,top-r.y},Rect{r.x,bottom,r.width,r.y+r.height-bottom},
            Rect{r.x,top,left-r.x,bottom-top},Rect{right,top,r.x+r.width-right,bottom-top}})
            if (part.width>0 && part.height>0) result.push_back(part);
    }
    return result;
}
void RecognitionCache::clear() { entries_.clear(); context_={}; width_=height_=0; }
std::vector<TextLine> RecognitionCache::read(Desktop& desktop,const Frame& frame,std::uint64_t generation,
    std::vector<Rect> regions,const std::function<bool()>& cancelled) {
    if (!frame.valid() || !generation || regions.size()>64) throw std::invalid_argument("Invalid OCR regions");
    auto check=[&] { if (cancelled && cancelled()) throw std::runtime_error("OCR cancelled"); };
    check();
    if (context_.window.id!=frame.window.id || context_.window.process!=frame.window.process || context_.window.bounds!=frame.window.bounds
        || context_.generation!=generation || width_!=frame.width || height_!=frame.height || frame.captured<context_.now) clear();
    context_={frame.window,generation,frame.captured}; width_=frame.width; height_=frame.height;
    for (auto& r:regions) {
        if (!r.valid() || r.x<0 || r.y<0 || r.x+r.width>1 || r.y+r.height>1) throw std::invalid_argument("OCR region is outside the frame");
        const int left=std::max(0,int(std::floor(r.x*frame.width))),top=std::max(0,int(std::floor(r.y*frame.height)));
        const int right=std::min(frame.width,int(std::ceil((r.x+r.width)*frame.width))),bottom=std::min(frame.height,int(std::ceil((r.y+r.height)*frame.height)));
        r={double(left),double(top),double(right-left),double(bottom-top)};
    }
    bool merged=true;
    while (merged) {
        merged=false;
        for (std::size_t i=0;i<regions.size() && !merged;++i) for (std::size_t j=i+1;j<regions.size();++j) {
            const auto a=regions[i],b=regions[j];
            if (a.x<=b.x+b.width && b.x<=a.x+a.width && a.y<=b.y+b.height && b.y<=a.y+a.height) {
                const Rect bounds{std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x+a.width,b.x+b.width)-std::min(a.x,b.x),std::max(a.y+a.height,b.y+b.height)-std::min(a.y,b.y)};
                const double overlap=std::max(0.,std::min(a.x+a.width,b.x+b.width)-std::max(a.x,b.x))
                    *std::max(0.,std::min(a.y+a.height,b.y+b.height)-std::max(a.y,b.y));
                if (bounds.width*bounds.height>a.width*a.height+b.width*b.height-overlap) continue;
                regions[i]=bounds; regions.erase(regions.begin()+j); merged=true; break;
            }
        }
    }
    std::vector<Entry> next; std::vector<std::size_t> changed;
    for (const auto& r:regions) {
        check();
        const auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& entry) { return entry.area==r; });
        Entry entry=found==entries_.end()?Entry{}:std::move(*found);
        const auto row_bytes=std::size_t(r.width)*4,bytes=row_bytes*std::size_t(r.height);
        bool same=found!=entries_.end() && entry.pixels.size()==bytes && frame.captured>=entry.recognized && frame.captured-entry.recognized<Time(5000);
        if (same) for (int y=0;y<int(r.height);++y) {
            const auto* source=frame.rgba.data()+(std::size_t(y+int(r.y))*frame.width+int(r.x))*4;
            if (std::memcmp(entry.pixels.data()+std::size_t(y)*row_bytes,source,row_bytes)) { same=false; break; }
        }
        if (!same) {
            entry.area=r; entry.pixels.resize(bytes); entry.lines.clear(); entry.recognized=frame.captured;
            for (int y=0;y<int(r.height);++y) std::memcpy(entry.pixels.data()+std::size_t(y)*row_bytes,
                frame.rgba.data()+(std::size_t(y+int(r.y))*frame.width+int(r.x))*4,row_bytes);
            changed.push_back(next.size());
        }
        next.push_back(std::move(entry));
    }
    if (!changed.empty()) {
        check();
        auto bounds=next[changed.front()].area;
        int packed_width=0,packed_height=8;
        for (const auto index:changed) {
            const auto r=next[index].area;
            bounds={std::min(bounds.x,r.x),std::min(bounds.y,r.y),
                std::max(bounds.x+bounds.width,r.x+r.width)-std::min(bounds.x,r.x),
                std::max(bounds.y+bounds.height,r.y+r.height)-std::min(bounds.y,r.y)};
            packed_width=std::max(packed_width,int(r.width)+16);
            packed_height+=int(r.height)+8;
        }
        // Disjoint labels use one OCR call, retaining their original screen coordinates.
        const bool packed=changed.size()>1 && double(packed_width)*packed_height<bounds.width*bounds.height;
        Frame crop{frame.window,frame.captured,packed?packed_width:int(bounds.width),packed?packed_height:int(bounds.height),{}};
        std::vector<Rect> placements;
        const bool single=changed.size()==1;
        if (!single) crop.rgba.resize(std::size_t(crop.width)*crop.height*4);
        for (std::size_t i=3;i<crop.rgba.size();i+=4) crop.rgba[i]=255;
        int cursor=8;
        for (const auto index:changed) {
            const auto& entry=next[index]; const auto r=entry.area;
            Rect box{packed?8:r.x-bounds.x,packed?double(cursor):r.y-bounds.y,r.width,r.height};
            placements.push_back(box);cursor+=int(r.height)+8;
            if (single) crop.rgba=std::move(next[index].pixels);
            else for (int y=0;y<int(r.height);++y) std::memcpy(crop.rgba.data()+(std::size_t(y+int(box.y))*crop.width+int(box.x))*4,
                entry.pixels.data()+std::size_t(y)*std::size_t(r.width)*4,std::size_t(r.width)*4);
        }
        auto lines=desktop.recognize(crop,cancelled);
        if (single) next[changed.front()].pixels=std::move(crop.rgba);
        check();
        if (lines.size()>4096) throw std::runtime_error("OCR exceeded text limits");
        for (const auto& line:lines) for (std::size_t j=0;j<changed.size();++j) {
            const auto& b=line.pixels;const auto& box=placements[j];auto& entry=next[changed[j]];
            if (b.x<box.x || b.y<box.y || b.x+b.width>box.x+box.width || b.y+b.height>box.y+box.height) continue;
            auto mapped=line;mapped.pixels.x+=entry.area.x-box.x;mapped.pixels.y+=entry.area.y-box.y;
            entry.lines.push_back(std::move(mapped));
        }
    }
    std::vector<TextLine> result;
    for (const auto& entry:next) {
        if (result.size()+entry.lines.size()>4096) throw std::runtime_error("OCR exceeded text limits");
        if (result.empty()) result=entry.lines;
        else for (const auto& line:entry.lines) {
            const auto duplicate=std::find_if(result.begin(),result.end(),[&](const auto& other) {
                const auto a=line.pixels,b=other.pixels;
                return line.text==other.text && std::abs(a.x-b.x)<=2 && std::abs(a.y-b.y)<=2
                    && std::abs(a.width-b.width)<=2 && std::abs(a.height-b.height)<=2;
            });
            if (duplicate==result.end()) result.push_back(line);
            else if (line.confidence.value_or(0)>duplicate->confidence.value_or(0)) *duplicate=line;
        }
    }
    check(); entries_=std::move(next); return result;
}
}
