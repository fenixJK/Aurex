#include "planters.hpp"
#include "nectar.hpp"
#include "menus.hpp"
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
struct Label { std::string type; double percent,resolution; };
std::optional<Label> label(const std::string& text) {
    if (text.size()>100) return {};
    std::string normalized;
    for (unsigned char c : text) {
        if (c>=128 || (c<32 && !std::isspace(c))) return {};
        if (std::isspace(c)) { if (!normalized.empty() && normalized.back()!=' ') normalized+=' '; }
        else normalized+=static_cast<char>(std::tolower(c));
    }
    if (!normalized.empty() && normalized.back()==' ') normalized.pop_back();
    static const std::regex pattern(R"(^([a-z -]+) ?\(([0-9]{1,3})(\.[0-9])?%\)$)");
    std::smatch match;
    if (!std::regex_match(normalized,match,pattern)) return {};
    auto name=match[1].str(); if (!name.empty() && name.back()==' ') name.pop_back();
    const auto type=std::find_if(types.begin(),types.end(),[&](const auto& t) { return name==t.first; });
    if (type==types.end()) return {};
    const auto digits=match[2].str();
    if (digits.size()>1 && digits.front()=='0') return {};
    const double percent=std::stod(digits+match[3].str());
    if (percent>100) return {};
    return Label{type->second,percent,match[3].matched?.1:1.};
}
int color(const Frame& f,int x,int y) {
    const auto p=(std::size_t(y)*f.width+x)*4;
    if (f.rgba[p+3]<250) return 0;
    const int r=f.rgba[p],g=f.rgba[p+1],b=f.rgba[p+2];
    if (g>=160 && g-r>=40 && g-b>=40) return 1;
    if (std::abs(r-86)<=15 && std::abs(g-120)<=15 && std::abs(b-72)<=15) return 2;
    return 0;
}
struct Row { int left,right,y; };
std::optional<Rect> bar(const DetectionInput& input,const Rect& text,double percent,std::size_t& budget) {
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
            if (!color(f,x,y)) { ++x; continue; }
            const int first=x; int filled=0,remaining=0; bool reversed=false;
            while (x<right) {
                const auto c=color(f,x,y); if (!c) break;
                filled+=c==1; remaining+=c==2;
                if (c==1 && remaining) reversed=true;
                ++x;
            }
            const int width=x-first;
            if (first==left || x==right || reversed || width<text.width*.9 || width>text.width+text.height*3
                || std::abs((first+x)/2.-(text.x+text.width/2))>text.height*.7) continue;
            if (percent==100) { if (filled!=width) continue; }
            else if (std::abs(100.*filled/width-percent)>std::max(3.,200./width)) continue;
            if (previous && previous->y+1==y && std::abs(previous->left-first)<=tolerance && std::abs(previous->right-x)<=tolerance) ++rows;
            else rows=1;
            previous=Row{first,x,y}; found=true;
            if (rows>=required) return Rect{double(first),double(y+1-rows),double(width),double(rows)};
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
Observation<std::string> read_interaction(const DetectionInput& input,const Observation<std::vector<VisiblePlanter>>& visible);
class PlanterDetector final : public Detector {
public:
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
    for (const auto& [name,type] : types) {
        if (input.cancelled && input.cancelled()) return {};
        const auto found=read_prompt_landmark(c,text,planter_harvest_prompt(type));
        uncertain|=found.knowledge==Knowledge::unknown;
        if (!found.has_value(c,Time(2000)) || !found.value) continue;
        if (result.knowledge==Knowledge::present && result.value!=type) return {};
        result={Knowledge::present,type,f.captured,f.window,input.generation};
    }
    if (input.cancelled && input.cancelled()) return {};
    if (result.knowledge==Knowledge::present) return result;
    if (uncertain) return {};
    const auto region=planter_harvest_prompt("PaperPlanter").region;
    for (const auto& line : input.text) {
        if (line.text.size()>512) return {};
        const auto& b=line.pixels;
        const double center=b.x+b.width/2;
        if (!b.valid() || center<f.width*region.x || center>f.width*(region.x+region.width)
            || b.y>=f.height*(region.y+region.height) || b.y+b.height<=f.height*region.y) continue;
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
Observation<std::string> read_planter_interaction(const DetectionInput& input) {
    return read_interaction(input,read_planters(input));
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
        const auto shape=bar(input,box,text->percent,budget);
        if (!budget || (input.cancelled && input.cancelled())) return {};
        if (!shape) continue;
        VisiblePlanter value{text->type,text->percent,text->percent==100?100:std::max(0.,text->percent-text->resolution/2),*shape};
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
    bool hud=false;
    static const std::regex reward(R"(^\+([0-9]{1,2}h)?([0-9]{1,2}m)?([0-9]{1,2}s)? of (comforting|refreshing|satisfying|motivating|invigorating) nectar \(from ([a-z -]+)\)$)");
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
        if (confident && b.y+b.height<f.height*.3 && (text=="pollen" || text.starts_with("pollen "))) hud=true;
        if (b.x<f.width*.5 || b.y<f.height*.4) continue;
        if (text.find("nectar")==std::string::npos || text.find("planter")==std::string::npos) continue;
        std::smatch match;
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
    if (result.value.empty() && (!hud || menu_controls(input).size()<2)) return {};
    if (input.cancelled && input.cancelled()) return {};
    return result;
}
std::shared_ptr<const Detector> planter_detector() { return std::make_shared<PlanterDetector>(); }
std::shared_ptr<const PlanterSource> planter_source(std::shared_ptr<const DetectionSource> source) {
    if (!source) throw std::invalid_argument("Planter observations need a detection source");
    return std::make_shared<ObservedPlanters>(std::move(source));
}
}
