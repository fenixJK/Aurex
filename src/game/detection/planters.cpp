#include "planters.hpp"
#include "nectar.hpp"
#include "menus.hpp"
#include "perception.hpp"
#include "bar_regions.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <array>

namespace hive::game {
namespace {
constexpr std::array types{
    std::pair{"paper planter","PaperPlanter"},std::pair{"ticket planter","TicketPlanter"},
    std::pair{"plastic planter","PlasticPlanter"},std::pair{"candy planter","CandyPlanter"},
    std::pair{"blue clay planter","BlueClayPlanter"},std::pair{"red clay planter","RedClayPlanter"},
    std::pair{"tacky planter","TackyPlanter"},std::pair{"pesticide planter","PesticidePlanter"},
    std::pair{"heat-treated planter","HeatTreatedPlanter"},std::pair{"hydroponic planter","HydroponicPlanter"},
    std::pair{"petal planter","PetalPlanter"},std::pair{"planter of plenty","PlanterOfPlenty"},
    std::pair{"the planter of plenty","PlanterOfPlenty"},std::pair{"festive planter","FestivePlanter"},
    std::pair{"sticker planter","StickerPlanter"}};
struct Label { std::string type; };
std::optional<Label> label(const std::string& text) {
    if (text.size()>100) return {};
    std::string normalized;
    for (unsigned char c : text) {
        if (c>=128 || (c<32 && !std::isspace(c))) return {};
        if (std::isspace(c)) { if (!normalized.empty() && normalized.back()!=' ') normalized+=' '; }
        else normalized+=static_cast<char>(std::tolower(c));
    }
    if (!normalized.empty() && normalized.back()==' ') normalized.pop_back();
    const auto suffix=normalized.find('(');
    auto name=normalized.substr(0,suffix);
    while (!name.empty() && name.back()==' ') name.pop_back();
    const auto type=std::find_if(types.begin(),types.end(),[&](const auto& t) { return name==t.first; });
    return type==types.end()?std::nullopt:std::optional<Label>{{type->second}};
}
int color(const std::uint8_t* pixel) {
    const int r=pixel[0],g=pixel[1],b=pixel[2];
    const bool filled=(g>=160) & (g-r>=40) & (g-b>=40);
    const bool remaining=(r>=60) & (r<=140) & (g>=85) & (g<=155) & (b>=40) & (b<=130)
        & (g-r>=8) & (g-b>=8);
    return (pixel[3]>=250)*(int(filled)+2*int(remaining));
}
struct Row { int left,right,y; };
struct GrowthBar { Rect bounds; double percent; };
std::optional<GrowthBar> bar(const DetectionInput& input,const Rect& text,std::size_t& budget) {
    const auto& f=input.frame;
    const int left=std::max(0,int(std::floor(text.x-text.height*2))),right=std::min(f.width,int(std::ceil(text.x+text.width+text.height*2)));
    const int top=std::max(0,int(std::floor(text.y-text.height*.7))),bottom=std::min(f.height,int(std::ceil(text.y+text.height*1.7)));
    const auto cost=std::size_t(right-left)*(bottom-top);
    if (cost>budget) { budget=0; return {}; } budget-=cost;
    std::optional<Row> previous;
    unsigned rows=0;
    const auto tolerance=std::max(2.,text.height*.15);
    const auto required=std::max(2u,static_cast<unsigned>(std::ceil(text.height*.1)));
    for (int y=top;y<bottom;++y) {
        if (input.cancelled && input.cancelled()) return {};
        bool found=false;
        for (int x=left;x<right;) {
            if (!color(f.rgba.data()+(std::size_t(y)*f.width+x)*4)) { ++x; continue; }
            const int first=x; int filled=0,remaining=0; bool reversed=false;
            while (x<right) {
                const auto c=color(f.rgba.data()+(std::size_t(y)*f.width+x)*4); if (!c) break;
                filled+=c==1; remaining+=c==2;
                if (c==1 && remaining) reversed=true;
                ++x;
            }
            const int width=x-first;
            if (first==left || x==right || reversed || width<text.width*.9 || width>text.width+text.height*3
                || std::abs((first+x)/2.-(text.x+text.width/2))>text.height*.7) continue;
            if (previous && previous->y+1==y && std::abs(previous->left-first)<=tolerance && std::abs(previous->right-x)<=tolerance) ++rows;
            else rows=1;
            previous=Row{first,x,y}; found=true;
            if (rows>=required) return GrowthBar{{double(first),double(y+1-rows),double(width),double(rows)},100.*filled/width};
            break;
        }
        if (!found) { previous.reset(); rows=0; }
    }
    return {};
}
bool same_bar(const VisiblePlanter& a,const VisiblePlanter& b) {
    return std::abs(a.bar.x-b.bar.x)<std::max(3.,a.bar.width*.05)
        && std::abs(a.bar.y-b.bar.y)<std::max(3.,a.bar.height*2.);
}
bool valid(const VisiblePlanter& p) {
    return std::any_of(types.begin(),types.end(),[&](const auto& t) { return p.type==t.second; })
        && std::isfinite(p.percent) && p.percent>=0 && p.percent<=100
        && std::isfinite(p.minimum_percent) && p.minimum_percent>=0 && p.minimum_percent<=p.percent && p.bar.valid();
}
bool labelled_growth_bar(const Frame& frame,Rect candidate) {
    const int left=int(candidate.x),right=int(candidate.x+candidate.width);
    const int top=int(candidate.y),bottom=int(candidate.y+candidate.height);
    // Scenery can touch the bar. Its outlined label and straight upper edge
    // identify the UI rectangle without treating that whole contour as the bar.
    const int radius=std::max(1,int(std::ceil(candidate.height*.12)));
    int first_x=right,last_x=left,first_y=bottom,last_y=top,white=0;
    auto labelled=[&] {
        const int width=last_x-first_x+1,height=last_y-first_y+1;
        if (white<8 || height<2 || width<height*8 || white>width*height*.65) return false;
        std::optional<Row> previous;
        unsigned rows=0;
        for (int y=std::max(top,first_y-height*2);y<first_y;++y) {
            const int middle=(first_x+last_x)/2;
            if (!color(frame.rgba.data()+(std::size_t(y)*frame.width+middle)*4)) { rows=0; previous.reset(); continue; }
            int a=middle,b=middle+1;
            while (a>left && color(frame.rgba.data()+(std::size_t(y)*frame.width+a-1)*4)) --a;
            while (b<right && color(frame.rgba.data()+(std::size_t(y)*frame.width+b)*4)) ++b;
            const int span=b-a;
            if (width<span*.35 || first_x<a+span*.05 || last_x>=b-span*.05) { rows=0; previous.reset(); continue; }
            const int tolerance=std::max(1,height/2);
            if (previous && std::abs(previous->left-a)<=tolerance && std::abs(previous->right-b)<=tolerance) ++rows;
            else rows=1;
            previous=Row{a,b,y};
            if (rows>=std::max(1,int(std::ceil(height*.2)))) return true;
        }
        return false;
    };
    for (int y=top;y<=bottom;++y) {
        int row_first=right,row_last=left,row_white=0;
        if (y<bottom) for (int x=left;x<right;++x) {
            const auto* p=frame.rgba.data()+(std::size_t(y)*frame.width+x)*4;
            if (p[3]<250 || std::min({p[0],p[1],p[2]})<120
                || std::max({p[0],p[1],p[2]})-std::min({p[0],p[1],p[2]})>40) continue;
            bool outlined=false;
            for (int dy=-radius;dy<=radius && !outlined;++dy) for (int dx=-radius;dx<=radius;++dx) {
                const int nx=x+dx,ny=y+dy;
                if (nx<left || nx>=right || ny<top || ny>=bottom) continue;
                const auto* edge=frame.rgba.data()+(std::size_t(ny)*frame.width+nx)*4;
                if (std::max({edge[0],edge[1],edge[2]})<100 && edge[1]<p[1]*.65) { outlined=true; break; }
            }
            if (outlined) { ++row_white; row_first=std::min(row_first,x); row_last=std::max(row_last,x); }
        }
        if (row_white>=2) {
            first_x=std::min(first_x,row_first); last_x=std::max(last_x,row_last);
            first_y=std::min(first_y,y); last_y=y; white+=row_white;
        } else {
            if (labelled()) return true;
            first_x=right; last_x=left; first_y=bottom; last_y=top; white=0;
        }
    }
    return false;
}
class PlanterPresenceDetector final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& frame=input.frame;
        if (!frame.valid() || !input.generation || (input.cancelled && input.cancelled())) return {};
        auto result=std::make_shared<Landmarks>();
        auto& visible=result->readings["planter.present"];
        visible={Knowledge::unknown,false,frame.captured,frame.window,input.generation};
        bool present=false;
        for (const auto& bar:detail::bar_regions(frame,color,true)) {
            if (input.cancelled && input.cancelled()) return {};
            if (labelled_growth_bar(frame,bar)) { present=true; break; }
        }
        if (!present) if (const auto region=interaction_prompt_region(frame))
            present=interaction_label(interaction_text_image(frame,*region),"Harvest").has_value();
        if (!present && !game_hud_visible(input.hud?*input.hud:inspect_hud(frame,input.generation))) return result;
        visible.knowledge=present?Knowledge::present:Knowledge::absent;
        visible.value=present;
        return result;
    }
};
Observation<std::string> read_interaction(const DetectionInput& input,const Observation<std::vector<VisiblePlanter>>& visible);
class PlanterDetector final : public Detector {
public:
    std::vector<Rect> text_regions(const Frame& frame,std::uint64_t) const override {
        std::vector<Rect> regions{{.5,.4,.5,.6}};
        for (const auto& bar:detail::bar_regions(frame,color)) regions.push_back(detail::label_region(frame,bar,1,1));
        if (const auto prompt=interaction_text_region(frame)) regions.push_back(*prompt);
        return regions;
    }

    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        auto result=std::make_shared<PlantersDetection>();
        if (!input.frame.valid() || (input.cancelled && input.cancelled())) return result;
        result->rewards=read_planter_rewards(input);
        result->visible=read_planters(input);
        result->interaction=read_interaction(input,result->visible);
        if (input.cancelled && input.cancelled()) return std::make_shared<PlantersDetection>();
        return result;
    }
};
class ObservedPlanters final : public PlanterSource {
    std::shared_ptr<const DetectionSource> source_;
public:
    explicit ObservedPlanters(std::shared_ptr<const DetectionSource> source):source_(std::move(source)) {}
    Observation<std::vector<PlanterReward>> rewards(const ObservationContext& c) const override {
        const auto p=source_->read();
        if (!p || !p->error.empty()) return {};
        const Observation<bool> envelope{Knowledge::present,true,p->context.now,p->context.window,p->context.generation};
        if (!envelope.has_value(c,Time(2000))) return {};
        const auto result=std::dynamic_pointer_cast<const PlantersDetection>(p->value);
        if (!result || !result->rewards.has_value(c,Time(2000)) || result->rewards.captured!=p->context.now) return {};
        return result->rewards;
    }
    Observation<std::string> interaction(const ObservationContext& c) const override {
        const auto p=source_->read();
        if (!p || !p->error.empty()) return {};
        const Observation<bool> envelope{Knowledge::present,true,p->context.now,p->context.window,p->context.generation};
        if (!envelope.has_value(c,Time(2000))) return {};
        const auto result=std::dynamic_pointer_cast<const PlantersDetection>(p->value);
        if (!result || !result->interaction.usable(c,Time(2000)) || result->interaction.captured!=p->context.now
            || (result->interaction.knowledge==Knowledge::present
                && std::none_of(types.begin(),types.end(),[&](const auto& type) { return type.second==result->interaction.value; }))
            || (result->interaction.knowledge==Knowledge::absent && !result->interaction.value.empty())) return {};
        return result->interaction;
    }
    Observation<std::vector<VisiblePlanter>> read(const ObservationContext& c) const override {
        const auto p=source_->read();
        if (!p || !p->error.empty()) return {};
        const Observation<bool> envelope{Knowledge::present,true,p->context.now,p->context.window,p->context.generation};
        if (!envelope.has_value(c,Time(2000))) return {};
        const auto result=std::dynamic_pointer_cast<const PlantersDetection>(p->value);
        if (!result || !result->visible.has_value(c,Time(2000)) || result->visible.captured!=p->context.now
            || result->visible.value.size()>16 || std::any_of(result->visible.value.begin(),result->visible.value.end(),[](const auto& p) { return !valid(p); })) return {};
        return result->visible;
    }
};
class PlanterProximity final : public LandmarkSource {
    std::shared_ptr<const PlanterSource> source_;
    std::string type_;
public:
    PlanterProximity(std::shared_ptr<const PlanterSource> source,std::string type):source_(std::move(source)),type_(std::move(type)) {
        if (!source_) throw std::invalid_argument("Planter proximity needs observations");
        planter_harvest_prompt(type_);
    }
    Observation<bool> read(const ObservationContext& c) const override {
        const auto seen=source_->interaction(c);
        if (seen.usable(c,Time(2000)) && seen.knowledge==Knowledge::absent && seen.value.empty())
            return {Knowledge::present,false,seen.captured,seen.window,seen.generation};
        if (!seen.has_value(c,Time(2000)) || seen.value!=type_) return {};
        return {Knowledge::present,true,seen.captured,seen.window,seen.generation};
    }
};
}
PromptLandmark planter_harvest_prompt(const std::string& type) {
    const auto found=std::find_if(types.begin(),types.end(),[&](const auto& item) { return type==item.second; });
    if (found==types.end()) throw std::invalid_argument("Unknown planter type");
    const auto prompt="Harvest the "+std::string(found->first);
    return {{prompt},{.2,0,.6,.3},.5f,PromptMatch::prefix,true};
}
namespace {
Observation<std::string> read_interaction(const DetectionInput& input,const Observation<std::vector<VisiblePlanter>>& visible) {
    const auto& f=input.frame;
    if (!f.valid() || !input.generation || f.width>8192 || f.height>8192 || input.text.empty() || input.text.size()>4096) return {};
    const ObservationContext c{f.window,input.generation,f.captured};
    const Observation<TextSnapshot> text{Knowledge::present,{f.width,f.height,{input.text.begin(),input.text.end()}},f.captured,f.window,input.generation};
    Observation<std::string> result;
    bool uncertain=false;
    auto region=interaction_prompt_region(f);
    const bool harvest=region && interaction_label(interaction_text_image(f,*region),"Harvest").has_value();
    if (harvest) {
        // OCR bounds can extend into the padding around the extracted text.
        const double left=std::max(0.,region->x-double(interaction_text_padding)/f.width);
        const double top=std::max(0.,region->y-double(interaction_text_padding)/f.height);
        const double right=std::min(1.,region->x+region->width+double(interaction_text_padding)/f.width);
        const double bottom=std::min(1.,region->y+region->height+double(interaction_text_padding)/f.height);
        region=Rect{left,top,right-left,bottom-top};
    }
    for (const auto& [name,type] : types) {
        if (input.cancelled && input.cancelled()) return {};
        auto prompt=planter_harvest_prompt(type);
        // Pixels identify the action; OCR only needs the equipment name in
        // that same banner, even if it misreads the word Harvest.
        if (harvest) { prompt.phrases={name}; prompt.region=*region; prompt.match=PromptMatch::phrase; }
        const auto found=read_prompt_landmark(c,text,prompt);
        uncertain|=found.knowledge==Knowledge::unknown;
        if (!found.has_value(c,Time(2000)) || !found.value) continue;
        if (result.knowledge==Knowledge::present && result.value!=type) return {};
        result={Knowledge::present,type,f.captured,f.window,input.generation};
    }
    if (input.cancelled && input.cancelled()) return {};
    if (result.knowledge==Knowledge::present) return result;
    if (uncertain) return {};
    const auto search=planter_harvest_prompt("PaperPlanter").region;
    for (const auto& line : input.text) {
        if (line.text.size()>512) return {};
        const auto& b=line.pixels;
        const double center=b.x+b.width/2;
        if (!b.valid() || center<f.width*search.x || center>f.width*(search.x+search.width)
            || b.y>=f.height*(search.y+search.height) || b.y+b.height<=f.height*search.y) continue;
        std::string lower=line.text;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c) { return std::tolower(c); });
        if (lower.find("harvest")!=std::string::npos || lower.find("planter")!=std::string::npos) return {};
    }
    if ((visible.has_value(c,Time(2000)) && !visible.value.empty()) || read_nectar(input).has_value(c,Time(2000)))
        result={Knowledge::absent,{},f.captured,f.window,input.generation};
    else {
        const auto controls=menu_controls(input);
        if (std::count_if(controls.begin(),controls.end(),[](const auto& control) { return control.selected.has_value(); })>=2)
            result={Knowledge::absent,{},f.captured,f.window,input.generation};
    }
    return result;
}
}
std::shared_ptr<const LandmarkSource> planter_proximity(std::shared_ptr<const PlanterSource> source,std::string type) {
    return std::make_shared<PlanterProximity>(std::move(source),std::move(type));
}
Observation<std::vector<VisiblePlanter>> read_planters(const DetectionInput& input) {
    const auto& f=input.frame;
    if (!f.valid() || !input.generation || f.width>8192 || f.height>8192 || input.text.size()>4096
        || (input.cancelled && input.cancelled())) return {};
    Observation<std::vector<VisiblePlanter>> result{Knowledge::present,{},f.captured,f.window,input.generation};
    std::size_t budget=4000000;
    for (const auto& line : input.text) {
        if (input.cancelled && input.cancelled()) return {};
        const auto& box=line.pixels;
        if (!box.valid() || box.x<0 || box.y<0 || box.x+box.width>f.width || box.y+box.height>f.height
            || box.height<6 || box.height>f.height*.15 || box.width<box.height*3 || box.width>f.width*.9
            || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.5f || *line.confidence>1))) continue;
        const auto text=label(line.text); if (!text) continue;
        const auto shape=bar(input,box,budget);
        if (!budget || (input.cancelled && input.cancelled())) return {};
        if (!shape) continue;
        VisiblePlanter value{text->type,shape->percent,shape->percent==100?100:std::max(0.,shape->percent-100./shape->bounds.width),shape->bounds};
        const auto duplicate=std::find_if(result.value.begin(),result.value.end(),[&](const auto& p) { return same_bar(value,p); });
        if (duplicate!=result.value.end()) {
            if (duplicate->type!=value.type || duplicate->percent!=value.percent) return {};
            continue;
        }
        result.value.push_back(std::move(value));
        if (result.value.size()>16) return {};
    }
    return result;
}
Observation<std::vector<PlanterReward>> read_planter_rewards(const DetectionInput& input) {
    const auto& f=input.frame;
    if (!f.valid() || !input.generation || f.width>8192 || f.height>8192 || input.text.size()>4096
        || (input.cancelled && input.cancelled())) return {};
    Observation<std::vector<PlanterReward>> result{Knowledge::present,{},f.captured,f.window,input.generation};
    const bool hud=game_hud_visible(input.hud?*input.hud:inspect_hud(f,input.generation));
    static const std::regex reward(R"(^\+([0-9]{1,2}h)?([0-9]{1,2}m)?([0-9]{1,2}s)? of (comforting|refreshing|satisfying|motivating|invigorating) nectar \(from ([a-z -]+)\)$)");
    static const std::regex item_reward(R"(^\+[1-9][0-9,]* [a-z][a-z '-]* \(from ([a-z -]+)\)$)");
    constexpr std::array names{"comforting","refreshing","satisfying","motivating","invigorating"};
    for (const auto& line : input.text) {
        if (input.cancelled && input.cancelled()) return {};
        const auto& b=line.pixels;
        if (!b.valid() || b.x<0 || b.y<0 || b.x+b.width>f.width || b.y+b.height>f.height || line.text.size()>256) continue;
        std::string text;
        for (unsigned char c : line.text) {
            if (std::isspace(c)) { if (!text.empty() && text.back()!=' ') text+=' '; }
            else text+=static_cast<char>(std::tolower(c));
        }
        if (!text.empty() && text.back()==' ') text.pop_back();
        const bool confident=!line.confidence || (std::isfinite(*line.confidence) && *line.confidence>=.8f && *line.confidence<=1);
        if (b.x<f.width*.5 || b.y<f.height*.4) continue;
        if (text.find("planter")==std::string::npos) continue;
        std::smatch match;
        if (text.find("nectar")==std::string::npos) {
            if (!confident || !std::regex_match(text,match,item_reward)) continue;
            const auto type=std::find_if(types.begin(),types.end(),[&](const auto& t) { return match[1].str()==t.first; });
            if (type==types.end()) continue;
            PlanterReward value{type->second,{},text};
            if (std::find(result.value.begin(),result.value.end(),value)==result.value.end()) result.value.push_back(std::move(value));
            if (result.value.size()>8) return {};
            continue;
        }
        if (!confident || !std::regex_match(text,match,reward)) return {};
        int duration=0;
        for (int i=1;i<=3;++i) if (match[i].matched) {
            const auto amount=std::stoi(match[i].str());
            if ((i>1 && amount>=60) || (i==1 && amount>24)) return {};
            duration+=amount*(i==1?3600:i==2?60:1);
        }
        if (!duration || duration>86400) return {};
        const auto type=std::find_if(types.begin(),types.end(),[&](const auto& t) { return match[5].str()==t.first; });
        const auto nectar=std::find(names.begin(),names.end(),match[4].str());
        if (type==types.end() || nectar==names.end()) return {};
        PlanterReward value{type->second,static_cast<Nectar>(nectar-names.begin()),text};
        if (std::find(result.value.begin(),result.value.end(),value)==result.value.end()) result.value.push_back(std::move(value));
        if (result.value.size()>8) return {};
    }
    if (result.value.empty() && !hud) return {};
    if (input.cancelled && input.cancelled()) return {};
    return result;
}
std::shared_ptr<const Detector> planter_detector() { return std::make_shared<PlanterDetector>(); }
std::shared_ptr<const Detector> planter_presence_detector() { return std::make_shared<PlanterPresenceDetector>(); }
std::shared_ptr<const PlanterSource> planter_source(std::shared_ptr<const DetectionSource> source) {
    if (!source) throw std::invalid_argument("Planter observations need a detection source");
    return std::make_shared<ObservedPlanters>(std::move(source));
}
}
