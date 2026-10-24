#include "inventory.hpp"
#include "menus.hpp"
#include "text.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <string_view>
#include <set>

namespace hive::game {
namespace {
bool point(Point p) { return std::isfinite(p.x) && std::isfinite(p.y) && p.x>=0 && p.x<1 && p.y>=0 && p.y<1; }
bool region(Rect r) { return r.valid() && r.x>=0 && r.y>=0 && r.x+r.width<=1 && r.y+r.height<=1; }
bool inside(Rect r,Point p) { return p.x>r.x && p.x<r.x+r.width && p.y>r.y && p.y<r.y+r.height; }
bool contains(Rect outer,Rect inner) { return inner.x>=outer.x && inner.y>=outer.y && inner.x+inner.width<=outer.x+outer.width && inner.y+inner.height<=outer.y+outer.height; }
bool overlaps(Rect a,Rect b) { return a.x<b.x+b.width && b.x<a.x+a.width && a.y<b.y+b.height && b.y<a.y+a.height; }
std::string_view trim(std::string_view text) {
    while (!text.empty() && text.front()==' ') text.remove_prefix(1);
    while (!text.empty() && text.back()==' ') text.remove_suffix(1);
    return text;
}
std::optional<std::uint64_t> integer(std::string_view text) {
    text=trim(text);
    if (text.empty() || text.size()>26 || (text.size()>1 && text.front()=='0')) return {};
    std::string digits;
    std::size_t group=0; bool grouped=false;
    for (char c : text) {
        if (c==',') {
            if (!group || (grouped?group!=3:group>3)) return {};
            grouped=true; group=0;
        } else {
            if (c<'0' || c>'9') return {};
            digits+=c; ++group;
        }
    }
    if (!group || (grouped && group!=3)) return {};
    std::uint64_t value{};
    const auto result=std::from_chars(digits.data(),digits.data()+digits.size(),value);
    if (result.ec!=std::errc{} || result.ptr!=digits.data()+digits.size()) return {};
    return value;
}
std::optional<InventoryQuantity> quantity(std::string_view text) {
    text=trim(text);
    if (text.empty() || text.size()>60) return {};
    if (text.front()=='x' || text.front()=='X') {
        const auto count=integer(text.substr(1));
        if (count) return InventoryQuantity{*count,{}};
        return {};
    }
    const auto slash=text.find('/');
    if (slash==std::string_view::npos) return {};
    const auto count=integer(text.substr(0,slash)),capacity=integer(text.substr(slash+1));
    if (!count || !capacity || !*capacity) return {};
    return InventoryQuantity{*count,*capacity};
}
Rect quantity_area(const InventoryRow& row) {
    return {row.bounds.x,row.bounds.y+row.bounds.height*0.45,row.title.x-row.bounds.x,row.bounds.height*0.55};
}
std::optional<InventoryQuantity> read_quantity(const DetectionInput& input,const InventoryRow& row) {
    const auto area=quantity_area(row);
    if (!area.valid()) return {};
    const TextLine* candidate=nullptr;
    for (const auto& line : input.text) {
        const Rect r{line.pixels.x/input.frame.width,line.pixels.y/input.frame.height,line.pixels.width/input.frame.width,line.pixels.height/input.frame.height};
        if (!r.valid() || !inside(area,{r.x+r.width/2,r.y+r.height/2})) continue;
        if (candidate || line.text.size()>60 || !contains(area,r) || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<0.5f || *line.confidence>1))) return {};
        candidate=&line;
    }
    if (!candidate) return {};
    const auto digits=std::count_if(candidate->text.begin(),candidate->text.end(),[](char c) { return c>='0' && c<='9'; });
    if (candidate->pixels.width<digits*candidate->pixels.height*0.5) return {};
    return quantity(candidate->text);
}
void refine_quantities(const DetectionInput& input,const InventoryLayout& layout,InventoryView& view,std::span<const std::string> wanted) {
    if (wanted.empty() || !input.recognize || !view.open) return;
    std::vector<std::pair<std::size_t,InventoryRow>> missing;
    for (std::size_t i=0;i<view.items.size();++i) if (!view.items[i].quantity && std::find(wanted.begin(),wanted.end(),inventory_name(view.items[i].name))!=wanted.end()) {
        const auto row=std::find_if(layout.rows.begin(),layout.rows.end(),[&](const auto& r) { return r.bounds==view.items[i].bounds; });
        if (row!=layout.rows.end()) missing.emplace_back(i,*row);
    }
    if (missing.empty()) return;
    int left=input.frame.width,top=input.frame.height,right=0,bottom=0;
    for (const auto& [index,row]:missing) {
        const auto area=quantity_area(row);
        left=std::min(left,int(std::floor(area.x*input.frame.width)));
        top=std::min(top,int(std::floor(area.y*input.frame.height)));
        right=std::max(right,int(std::ceil((area.x+area.width)*input.frame.width)));
        bottom=std::max(bottom,int(std::ceil((area.y+area.height)*input.frame.height)));
    }
    const int padding=std::max(2,input.frame.height/300);
    left=std::max(0,left-padding); top=std::max(0,top-padding);
    right=std::min(input.frame.width,right+padding); bottom=std::min(input.frame.height,bottom+padding);
    Frame crop{input.frame.window,input.frame.captured,right-left,bottom-top,{}};
    crop.rgba.assign(std::size_t(crop.width)*crop.height*4,255);
    for (const auto& [index,row]:missing) {
        const auto area=quantity_area(row);
        for (int y=int(std::ceil(area.y*input.frame.height));y<int((area.y+area.height)*input.frame.height);++y) {
            if (input.cancelled && input.cancelled()) return;
            for (int x=int(std::ceil(area.x*input.frame.width));x<int((area.x+area.width)*input.frame.width);++x) {
                const auto source=(std::size_t(y)*input.frame.width+x)*4;
                const auto lo=std::min({input.frame.rgba[source],input.frame.rgba[source+1],input.frame.rgba[source+2]});
                const auto hi=std::max({input.frame.rgba[source],input.frame.rgba[source+1],input.frame.rgba[source+2]});
                if (lo>=230 && hi-lo<=20) {
                    const auto target=(std::size_t(y-top)*crop.width+x-left)*4;
                    crop.rgba[target]=crop.rgba[target+1]=crop.rgba[target+2]=0;
                }
            }
        }
        const int row_top=int(std::ceil(area.y*input.frame.height)),row_bottom=int((area.y+area.height)*input.frame.height);
        for (int x=int(std::ceil(area.x*input.frame.width));x<int((area.x+area.width)*input.frame.width);++x) {
            unsigned ink=0;
            for (int y=row_top;y<row_bottom;++y) ink+=crop.rgba[(std::size_t(y-top)*crop.width+x-left)*4]==0;
            if (ink<(row_bottom-row_top)*.85) continue;
            for (int y=row_top;y<row_bottom;++y) {
                const auto target=(std::size_t(y-top)*crop.width+x-left)*4;
                crop.rgba[target]=crop.rgba[target+1]=crop.rgba[target+2]=255;
            }
        }
    }
    auto text=input.recognize(crop);
    if (text.size()>2048 || (input.cancelled && input.cancelled())) return;
    for (auto& line:text) { line.pixels.x+=left; line.pixels.y+=top; }
    const DetectionInput refined{input.frame,text,input.generation,input.cancelled};
    for (const auto& [index,row]:missing) view.items[index].quantity=read_quantity(refined,row);
    for (const auto& [index,row]:missing) if (!view.items[index].quantity) {
        if (input.cancelled && input.cancelled()) return;
        const auto area=quantity_area(row);
        int x0=crop.width,y0=crop.height,x1=0,y1=0;
        for (int y=int(std::ceil(area.y*input.frame.height))-top;y<int((area.y+area.height)*input.frame.height)-top;++y)
            for (int x=int(std::ceil(area.x*input.frame.width))-left;x<int((area.x+area.width)*input.frame.width)-left;++x)
                if (crop.rgba[(std::size_t(y)*crop.width+x)*4]==0) {
                    x0=std::min(x0,x); y0=std::min(y0,y); x1=std::max(x1,x+1); y1=std::max(y1,y+1);
                }
        if (x1-x0<4 || y1-y0<5) continue;
        const int border=std::max(4,int(std::ceil((y1-y0)*.4)));
        Frame isolated{input.frame.window,input.frame.captured,x1-x0+2*border,y1-y0+2*border,{}};
        isolated.rgba.assign(std::size_t(isolated.width)*isolated.height*4,255);
        for (int y=y0;y<y1;++y) std::copy_n(crop.rgba.begin()+(std::size_t(y)*crop.width+x0)*4,(x1-x0)*4,
            isolated.rgba.begin()+(std::size_t(y-y0+border)*isolated.width+border)*4);
        auto focused=input.recognize(isolated);
        if (focused.size()>32) continue;
        for (auto& line:focused) { line.pixels.x+=left+x0-border; line.pixels.y+=top+y0-border; }
        view.items[index].quantity=read_quantity({input.frame,focused,input.generation,input.cancelled},row);
    }
}
class Reader final : public Detector {
    std::shared_ptr<const InventoryLayoutSource> layout_;
    mutable Observation<InventoryLayout> previous_;
    std::shared_ptr<InventoryRequests> requests_;
public:
    Reader(std::shared_ptr<const InventoryLayoutSource> layout,std::shared_ptr<InventoryRequests> requests):layout_(std::move(layout)),requests_(std::move(requests)) {}
    std::vector<Rect> text_regions(const Frame& frame,std::uint64_t generation) const override { return text_regions({frame,{},generation,{}}); }
    std::vector<Rect> text_regions(const DetectionInput& input) const override {
        const auto& frame=input.frame; const auto generation=input.generation;
        const ObservationContext context{frame.window,generation,frame.captured};
        const auto layout=layout_->read(input);
        if (layout.has_value(context,Time(0)) && layout.value.open) return {layout.value.viewport};
        if (previous_.has_value(context,Time(2000)) && previous_.value.open) return {previous_.value.viewport};
        const auto sidebar=sidebar_region(input,MenuTab::inventory);
        return sidebar?std::vector<Rect>{*sidebar}:std::vector<Rect>{};
    }
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        if (input.cancelled && input.cancelled()) return {};
        const auto layout=layout_->read(input);
        previous_=layout;
        auto result=std::make_shared<InventoryDetection>(); result->view=read_inventory(input,layout);
        if (requests_ && result->view.knowledge==Knowledge::present)
            refine_quantities(input,layout.value,result->view.value,requests_->quantities({input.frame.window,input.generation,input.frame.captured}));
        if (input.cancelled && input.cancelled()) return {};
        return result;
    }
};
class Source final : public InventorySource {
    std::shared_ptr<ItemStock> stock_;
    std::shared_ptr<const DetectionSource> source_;
    std::shared_ptr<InventoryRequests> requests_;
public:
    Source(std::shared_ptr<const DetectionSource> source,std::shared_ptr<InventoryRequests> requests,std::shared_ptr<ItemStock> stock):stock_(std::move(stock)),source_(std::move(source)),requests_(std::move(requests)) {}
    void request_quantities(const ObservationContext& c,std::span<const std::string> names) const override {
        if (requests_) requests_->quantities(c,names);
    }
    Observation<InventoryView> read(const ObservationContext& c,const State&) const override {
        // Reading the channel also requests its next frame. Let the layout reader
        // resolve the panel and toggle together instead of overriding it here.
        auto result=source_->read(); if (!result || !result->error.empty()) return {};
        const auto inventory=std::dynamic_pointer_cast<const InventoryDetection>(result->value);
        if (!inventory || !inventory->view.has_value(result->context,Time(0))) return {};
        if (stock_ && inventory->view.has_value(c,Time(2000))) {
            const auto reserves=stock_->read().reserves;
            for (const auto& item:inventory->view.value.items) if (item.quantity)
                for (const auto& [name,count]:reserves) if (inventory_name(name)==inventory_name(item.name)) stock_->observe(name,item.quantity->count,inventory->view.captured);
        }
        return inventory->view;
    }
};
}
std::string inventory_name(const std::string& text) {
    if (text.size()>160) return {};
    std::string result;
    std::string_view remaining=text;
    while (!remaining.empty()) {
        // OCR may substitute typographic spaces, hyphens or apostrophes.
        // Ignore those like their ASCII equivalents; never discard unknown
        // Unicode letters, which could turn a different item into a match.
        static constexpr std::string_view punctuation[]{"\xc2\xa0","\xc2\xad",
            "\xe2\x80\x90","\xe2\x80\x91","\xe2\x80\x92","\xe2\x80\x93","\xe2\x80\x94","\xe2\x80\x95",
            "\xe2\x80\x98","\xe2\x80\x99","\xe2\x80\xaf"};
        if (static_cast<unsigned char>(remaining.front())>=128) {
            const auto found=std::find_if(std::begin(punctuation),std::end(punctuation),
                [&](auto mark) { return remaining.starts_with(mark); });
            if (found==std::end(punctuation)) return {};
            remaining.remove_prefix(found->size()); continue;
        }
        const unsigned char c=remaining.front(); remaining.remove_prefix(1);
        if (c<32 && !std::isspace(c)) return {};
        if (std::isalnum(c)) result+=static_cast<char>(std::tolower(c));
    }
    return result;
}
void validate(const InventoryView& view) {
    if (view.items.size()>32 || view.target.size()>120 || (view.toggle && !point(*view.toggle)) || (view.drop && !point(*view.drop)))
        throw std::invalid_argument("Invalid inventory view");
    if (!view.open) { if (!view.items.empty() || view.readable) throw std::invalid_argument("Closed inventory contains visible items"); return; }
    if (!region(view.viewport) || (view.drop && inside(view.viewport,*view.drop))) throw std::invalid_argument("Invalid inventory viewport or drop target");
    std::vector<Rect> rows;
    for (const auto& item : view.items) {
        if (inventory_name(item.name).empty() || !region(item.bounds) || !contains(view.viewport,item.bounds) || !point(item.grab) || !inside(item.bounds,item.grab)
            || std::any_of(rows.begin(),rows.end(),[&](Rect r) { return overlaps(r,item.bounds); })) throw std::invalid_argument("Invalid inventory item row");
        if (item.quantity && item.quantity->capacity && !*item.quantity->capacity)
            throw std::invalid_argument("Invalid inventory quantity");
        rows.push_back(item.bounds);
    }
}
Observation<InventoryView> read_inventory(const DetectionInput& input,const Observation<InventoryLayout>& layout) {
    if (!input.frame.valid() || !input.frame.window.id || !input.generation || input.text.size()>2048) throw std::invalid_argument("Invalid inventory detection input");
    if (input.cancelled && input.cancelled()) return {};
    const ObservationContext context{input.frame.window,input.generation,input.frame.captured};
    if (!layout.has_value(context,Time(0))) return {};
    const auto& l=layout.value;
    InventoryView view{l.open,l.at_top,l.at_bottom,false,l.viewport,l.toggle,l.drop,l.target,{}};
    validate(view); if (l.rows.size()>32) throw std::invalid_argument("Too many inventory rows");
    if (l.open) {
        view.readable=!l.rows.empty(); std::vector<Rect> occupied;
        for (const auto& row : l.rows) {
            if (input.cancelled && input.cancelled()) return {};
            if (!region(row.bounds) || !region(row.title) || !contains(l.viewport,row.bounds) || !contains(row.bounds,row.title) || !inside(row.bounds,row.grab)
                || std::any_of(occupied.begin(),occupied.end(),[&](Rect r) { return overlaps(r,row.bounds); })) throw std::invalid_argument("Ambiguous inventory layout");
            occupied.push_back(row.bounds); std::vector<TextLine> words; bool complete=true;
            for (const auto& line : input.text) {
                const Rect r{line.pixels.x/input.frame.width,line.pixels.y/input.frame.height,line.pixels.width/input.frame.width,line.pixels.height/input.frame.height};
                if (!r.valid() || !inside(row.title,{r.x+r.width/2,r.y+r.height/2})) continue;
                if (!contains(row.bounds,r) || line.text.size()>160 || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<0.65f || *line.confidence>1))) { complete=false; continue; }
                words.push_back(line);
            }
            std::string name;
            for (const auto& word : text_candidates(words,1)) { if (!name.empty()) name+=' '; name+=word.text; }
            if (!complete || inventory_name(name).empty()) { view.readable=false; continue; }
            view.items.push_back({name,row.bounds,row.grab,read_quantity(input,row)});
        }
    }
    if (input.cancelled && input.cancelled()) return {};
    validate(view); return {Knowledge::present,std::move(view),context.now,context.window,context.generation};
}
void InventoryRequests::quantities(const ObservationContext& c,std::span<const std::string> names) {
    std::vector<std::string> requested;
    if (names.size()>32) throw std::invalid_argument("Too many inventory quantity requests");
    for (const auto& name:names) {
        if (name.empty() || name.size()>160) throw std::invalid_argument("Invalid inventory quantity request");
        requested.push_back(inventory_name(name));
    }
    std::lock_guard lock(mutex_); context_=c; names_=std::move(requested);
}
std::vector<std::string> InventoryRequests::quantities(const ObservationContext& c) const {
    std::lock_guard lock(mutex_);
    if (c.generation!=context_.generation || c.window.id!=context_.window.id || c.window.process!=context_.window.process
        || c.window.bounds!=context_.window.bounds || std::chrono::abs(c.now-context_.now)>Time(2000)) return {};
    return names_;
}
std::shared_ptr<const Detector> inventory_detector(std::shared_ptr<const InventoryLayoutSource> layout,std::shared_ptr<InventoryRequests> requests) {
    if (!layout) throw std::invalid_argument("Inventory detection needs an observed layout"); return std::make_shared<Reader>(std::move(layout),std::move(requests));
}
std::shared_ptr<const InventorySource> inventory_source(std::shared_ptr<const DetectionSource> channel,std::shared_ptr<InventoryRequests> requests,std::shared_ptr<ItemStock> stock) {
    if (!channel) throw std::invalid_argument("Inventory detection channel is missing"); return std::make_shared<Source>(std::move(channel),std::move(requests),std::move(stock));
}
std::vector<InventoryRow> inventory_rows(const Frame& frame,Rect viewport) {
    if (!frame.valid() || !region(viewport)) throw std::invalid_argument("Invalid inventory row scan");
    const auto top=static_cast<int>(std::ceil(viewport.y*frame.height)),bottom=static_cast<int>(std::floor((viewport.y+viewport.height)*frame.height));
    const auto left=static_cast<int>((viewport.x+viewport.width*0.985)*frame.width),right=static_cast<int>((viewport.x+viewport.width)*frame.width);
    if (right<=left) return {};
    std::vector<std::pair<int,int>> bands; int start=-1;
    for (int y=top;y<=bottom;++y) {
        int light=0;
        if (y<bottom) for (int x=left;x<right;++x) {
            const auto index=(static_cast<std::size_t>(y)*frame.width+x)*4;
            const auto r=frame.rgba[index],g=frame.rgba[index+1],b=frame.rgba[index+2];
            light+=std::min({r,g,b})>=200 && std::max({r,g,b})-std::min({r,g,b})<=25;
        }
        const bool card=y<bottom && light>=0.25*(right-left);
        if (card && start<0) start=y;
        if (!card && start>=0) { if (y-start>=10) bands.push_back({start,y}); start=-1; }
    }
    if (bands.size()<2 || bands.size()>32) return {};
    std::vector<int> heights; for (const auto& [a,b] : bands) heights.push_back(b-a);
    std::sort(heights.begin(),heights.end()); const auto height=heights[heights.size()/2];
    std::vector<InventoryRow> result;
    for (const auto& [a,b] : bands) if (a>top && b<bottom && b-a>=height*0.85 && b-a<=height*1.2) {
        const Rect row{viewport.x,double(a)/frame.height,viewport.width,double(b-a)/frame.height};
        result.push_back({row,{row.x+row.width*0.32,row.y,row.width*0.67,row.height*0.35},{row.x+row.width*0.14,row.y+row.height*0.5}});
    }
    return result;
}
}
