#include "enemies.hpp"
#include "game/data/fields.hpp"
#include "bar_regions.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <regex>

namespace hive::game {
std::shared_ptr<const EnemiesDetection> read_enemies(const DetectionSource& source,const ObservationContext& context,Time since) {
    const auto packet=source.read();
    if (!packet || !packet->error.empty()) return {};
    const Observation<bool> envelope{Knowledge::present,true,packet->context.now,packet->context.window,packet->context.generation};
    if (!envelope.has_value(context,Time(2000),since)) return {};
    auto result=std::dynamic_pointer_cast<const EnemiesDetection>(packet->value);
    return result && result->visible.has_value(context,Time(2000),since)?result:nullptr;
}
namespace {
constexpr std::array<const char*,13> names{"Ladybug","Rhino Beetle","Mantis","Spider","Scorpion","Werewolf",
    "Vicious Bee","Mondo Chick","King Beetle","Tunnel Bear","Coconut Crab","Stump Snail","Commando Chick"};
std::string normalized(const std::string& input) {
    std::string result;
    for (unsigned char c : input) {
        if (std::isalnum(c)) result+=static_cast<char>(std::tolower(c));
        else if (!result.empty() && result.back()!=' ') result+=' ';
    }
    if (!result.empty() && result.back()==' ') result.pop_back();
    return result;
}
bool confidence(const TextLine& line,float minimum) {
    return !line.confidence || (std::isfinite(*line.confidence) && *line.confidence>=minimum && *line.confidence<=1);
}
int health_pixel(const std::uint8_t* pixel) {
    const int r=pixel[0],g=pixel[1],b=pixel[2];
    const bool filled=(r<=66) & (g>=196) & (b>=28) & (b<=108);
    const bool remaining=(r>=82) & (r<=132) & (g>=1) & (g<=37) & (b>=6) & (b<=46);
    return (pixel[3]>=251)*(int(filled)+2*int(remaining));
}
bool notification_ink(const DetectionInput& input,const Rect& box,std::size_t& budget) {
    const auto& frame=input.frame;
    const int left=std::max(0,int(std::floor(box.x))),right=std::min(frame.width,int(std::ceil(box.x+box.width)));
    const int top=std::max(0,int(std::floor(box.y))),bottom=std::min(frame.height,int(std::ceil(box.y+box.height)));
    const auto cost=static_cast<std::size_t>(right-left)*(bottom-top);
    if (cost>=budget) { budget=0; return false; }
    budget-=cost;
    int yellow=0,min_x=right,max_x=left,min_y=bottom,max_y=top;
    for (int y=top;y<bottom;++y) {
        if (input.cancelled && input.cancelled()) return false;
        for (int x=left;x<right;++x) {
            const auto at=(static_cast<std::size_t>(y)*frame.width+x)*4;
            const int r=frame.rgba[at],g=frame.rgba[at+1],b=frame.rgba[at+2];
            if (frame.rgba[at+3]>=251 && r>=150 && g>=130 && r>b+60 && g>b+50 && std::abs(r-g)<80) {
                ++yellow; min_x=std::min(min_x,x); max_x=std::max(max_x,x); min_y=std::min(min_y,y); max_y=std::max(max_y,y);
            }
        }
    }
    return yellow>=box.width*box.height*.03 && max_x-min_x>=box.width*.5 && max_y-min_y>=box.height*.25;
}
struct HealthBar { Rect bounds; double fraction; };
std::optional<HealthBar> health_bar(const DetectionInput& input,const Rect& label,std::size_t& budget) {
    const auto& frame=input.frame;
    const int left=std::max(0,int(std::floor(label.x-label.height*.4)));
    const int right=std::min(frame.width,int(std::ceil(label.x+label.width+label.height*.4)));
    const int top=std::max(0,int(std::floor(label.y+label.height*.6)));
    const int bottom=std::min(frame.height,int(std::ceil(label.y+label.height*2.8)));
    const auto cost=static_cast<std::size_t>(right-left)*(bottom-top);
    if (cost>=budget) { budget=0; return {}; }
    budget-=cost;
    int rows=0,first_y=0,min_x=right,max_x=left,filled=0,colored=0;
    const int required=std::max(2,int(std::ceil(label.height*.12)));
    for (int y=top;y<bottom;++y) {
        if (input.cancelled && input.cancelled()) return {};
        int first=right,last=left,total=0,green=0;
        for (int x=left;x<right;++x) if (const auto color=health_pixel(frame.rgba.data()+(std::size_t(y)*frame.width+x)*4)) {
            first=std::min(first,x); last=x; ++total; green+=color==1;
        }
        const int width=last-first+1;
        if (width>=label.width*.55 && width<=label.width*1.4 && total>=width*.55 && green>=2
            && std::abs((first+last)/2.0-(label.x+label.width/2))<=label.height*.7) {
            if (!rows) { first_y=y; min_x=first; max_x=last; filled=colored=0; }
            else if (std::abs(first-min_x)>label.height*.35 || std::abs(last-max_x)>label.height*.35) {
                rows=0; continue;
            }
            min_x=std::min(min_x,first); max_x=std::max(max_x,last);
            filled+=green; colored+=total;
            if (++rows>=required) return HealthBar{{double(min_x),double(first_y),double(max_x-min_x+1),double(rows)},double(filled)/colored};
        } else rows=0;
    }
    return {};
}
class Enemies final : public Detector {
    mutable ObservationContext previous_;
    mutable std::map<std::string,unsigned> counts_;
    mutable std::map<std::string,Time> cleared_;
    mutable std::vector<EnemyDefeat> recent_;
    mutable std::uint64_t sequence_{};
public:
    std::vector<Rect> text_regions(const Frame& frame,std::uint64_t) const override {
        std::vector<Rect> regions{{.5,.4,.5,.6}};
        for (const auto& bar:detail::bar_regions(frame,health_pixel,true)) regions.push_back(detail::label_region(frame,bar,3,0.5));
        return regions;
    }

    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& frame=input.frame;
        if (!frame.valid() || !input.generation || frame.width>8192 || frame.height>8192 || input.text.size()>4096
            || (input.cancelled && input.cancelled())) return {};
        auto result=std::make_shared<EnemiesDetection>();
        result->visible={Knowledge::present,{},frame.captured,frame.window,input.generation};
        for (const auto* name : names) result->defeats[name]={Knowledge::absent,false,frame.captured,frame.window,input.generation};
        // Identify the species at the start of its label. Level text is unused
        // and must never prevent a name plus its health bar from being recognized.
        static const std::regex label(R"(^(?:gifted )?(ladybug|rhino beetle|mantis|spider|scorpion|werewolf|vicious bee|mondo chick|king beetle|tunnel bear|coconut crab|stump snail|commando chick)(?: |$))");
        static const std::regex boss_defeat(R"(^(?:gifted )?(vicious bee|mondo chick|king beetle|tunnel bear|coconut crab|stump snail|commando chick) has been defeated$)");
        static const std::regex defeat(R"(^(?:you )?defeated (ladybug|rhino beetle|mantis|spider|scorpion|werewolf|vicious bee|mondo chick|king beetle|tunnel bear|coconut crab|stump snail|commando chick)(?: ([1-9][0-9]{0,7}) battle points?)?$)");
        static const std::regex attacking(R"(^(?:gifted )?vicious bee is attacking .{1,40} in (?:the )?(.+?)(?: field| patch| forest)?$)");
        std::map<std::string,unsigned> counts;
        std::vector<std::pair<std::string,Rect>> notifications;
        std::size_t budget=4000000;
        for (const auto& line : input.text) {
            if (input.cancelled && input.cancelled()) return {};
            const auto& box=line.pixels;
            if (!box.valid() || line.text.size()>512 || box.x<-.5 || box.y<-.5
                || box.x+box.width>frame.width+.5 || box.y+box.height>frame.height+.5) {
                for (auto& [name,reading] : result->defeats) if (reading.knowledge!=Knowledge::present) reading.knowledge=Knowledge::unknown;
                continue;
            }
            const auto text=normalized(line.text);
            std::smatch match;
            std::string species;
            std::optional<HealthBar> health;
            if (confidence(line,.3f) && box.y>=frame.height*.1 && box.height>=5 && box.height<=frame.height*.12
                && box.width<=frame.width*.5 && std::regex_search(text,match,label)) {
                species=match[1];health=health_bar(input,box,budget);
            }
            const bool enemy_label=health.has_value();
            if (box.x>=frame.width*.5 && !enemy_label) {
                auto notice=text;
                if (confidence(line,.8f) && (text.starts_with("vicious bee ") || text.starts_with("gifted vicious bee "))
                    && !std::regex_match(notice,match,attacking) && !std::regex_match(notice,match,boss_defeat)) {
                    for (const auto& continuation:input.text) {
                        const auto& next=continuation.pixels;
                        if (!next.valid() || next.x<frame.width*.5 || next.x+next.width>frame.width+.5
                            || next.y<box.y+box.height*.8 || next.y>box.y+box.height*2 || next.y+next.height>frame.height+.5
                            || continuation.text.size()>160 || !confidence(continuation,.8f)) continue;
                        const auto joined=text+" "+normalized(continuation.text);
                        if (std::regex_match(joined,match,attacking) || std::regex_match(joined,match,boss_defeat)) { notice=joined; break; }
                    }
                }
                if (confidence(line,.8f) && std::regex_match(notice,match,attacking)) {
                    for (const auto& field:fields()) if (normalized(field.name)==match[1]) {
                        if (std::none_of(result->activity.begin(),result->activity.end(),[&](const auto& item) { return item.name=="Vicious Bee" && item.field==field.id; }))
                            result->activity.push_back({"Vicious Bee",field.id});
                        break;
                    }
                }
                if ((std::regex_match(notice,match,defeat) || std::regex_match(notice,match,boss_defeat)) && (confidence(line,.8f)
                    || (confidence(line,.5f) && notification_ink(input,box,budget)))) {
                    for (std::size_t i=0;i<names.size();++i) if (const auto* name=names[i]; normalized(name)==match[1]
                        && (i>=6 || match[2].matched || notification_ink(input,box,budget))) {
                        auto& reading=result->defeats.at(name); reading.knowledge=Knowledge::present; reading.value=true;
                        const auto duplicate=std::any_of(notifications.begin(),notifications.end(),[&](const auto& item) {
                            return item.first==name && std::abs(item.second.y-box.y)<box.height*.6;
                        });
                        if (!duplicate) { ++counts[name]; notifications.emplace_back(name,box); }
                    }
                } else if (text.find("defeat")!=std::string::npos || !confidence(line,.8f)) {
                    for (const auto* name : names) if (text.find(normalized(name))!=std::string::npos) {
                        auto& reading=result->defeats.at(name);
                        if (reading.knowledge!=Knowledge::present) reading.knowledge=Knowledge::unknown;
                    }
                }
            }
            if (!budget || (input.cancelled && input.cancelled())) return {};
            if (!health) continue;
            for (std::size_t i=0;i<names.size();++i) if (const auto* name=names[i]; normalized(name)==species) {
                const auto duplicate=std::any_of(result->visible.value.begin(),result->visible.value.end(),[&](const auto& enemy) {
                    return enemy.name==name && std::abs(enemy.health_bar.x-health->bounds.x)<box.height
                        && std::abs(enemy.health_bar.y-health->bounds.y)<box.height;
                });
                if (!duplicate) result->visible.value.push_back({name,health->bounds,health->fraction});
            }
            if (result->visible.value.size()>64) return {};
        }
        const bool continuity=previous_.generation==input.generation && previous_.window.id==frame.window.id
            && previous_.window.process==frame.window.process && previous_.window.bounds==frame.window.bounds
            && frame.captured>=previous_.now && frame.captured-previous_.now<Time(5000);
        if (!continuity) { recent_.clear(); counts_=counts; cleared_.clear(); }
        else for (const auto* name:names) {
            const auto count=counts[name],before=counts_[name];
            if (count>=before) {
                for (auto i=before;i<count;++i) recent_.push_back({name,frame.captured,++sequence_});
                counts_[name]=count; cleared_.erase(name);
            } else if (result->defeats.at(name).knowledge!=Knowledge::unknown) {
                const auto [it,inserted]=cleared_.try_emplace(name,frame.captured);
                if (frame.captured-it->second>=Time(2000)) { counts_[name]=count; cleared_.erase(it); }
            }
        }
        std::erase_if(recent_,[&](const auto& event) { return frame.captured-event.seen>Time(180000); });
        if (recent_.size()>64) recent_.erase(recent_.begin(),recent_.end()-64);
        previous_={frame.window,input.generation,frame.captured};
        result->recent_defeats=recent_;
        return result;
    }
};
std::shared_ptr<const EnemiesDetection> packet(const DetectionSource& source,const ObservationContext& c) {
    const auto p=source.read();
    if (!p || !p->error.empty()) return {};
    const Observation<bool> envelope{Knowledge::present,true,p->context.now,p->context.window,p->context.generation};
    if (!envelope.has_value(c,Time(2000))) return {};
    const auto result=std::dynamic_pointer_cast<const EnemiesDetection>(p->value);
    if (!result || !result->visible.has_value(c,Time(2000)) || result->visible.captured!=p->context.now) return {};
    return result;
}
class ObservedEnemies final : public EnemySource {
    std::shared_ptr<const DetectionSource> source_;
public:
    explicit ObservedEnemies(std::shared_ptr<const DetectionSource> source):source_(std::move(source)) {}
    Observation<std::vector<VisibleEnemy>> read(const ObservationContext& c) const override {
        const auto result=packet(*source_,c); return result?result->visible:Observation<std::vector<VisibleEnemy>>{};
    }
};
class Defeat final : public LandmarkSource {
    std::shared_ptr<const DetectionSource> source_;
    std::string name_;
public:
    Defeat(std::shared_ptr<const DetectionSource> source,std::string name):source_(std::move(source)),name_(std::move(name)) {}
    Observation<bool> read(const ObservationContext& c) const override {
        const auto result=packet(*source_,c);
        if (!result) return {};
        const auto it=result->defeats.find(name_);
        if (it==result->defeats.end() || it->second.captured!=result->visible.captured || !it->second.usable(c,Time(2000))) return {};
        return it->second;
    }
};
}
std::shared_ptr<const Detector> enemy_detector() { return std::make_shared<Enemies>(); }
std::shared_ptr<const EnemySource> enemy_source(std::shared_ptr<const DetectionSource> source) {
    if (!source) throw std::invalid_argument("Enemy observations need a detection source");
    return std::make_shared<ObservedEnemies>(std::move(source));
}
std::shared_ptr<const LandmarkSource> enemy_defeat_source(std::shared_ptr<const DetectionSource> source,const std::string& name) {
    if (!source || std::none_of(names.begin(),names.end(),[&](const auto* value) { return name==value; }))
        throw std::invalid_argument("Enemy result needs a detection source and supported enemy");
    return std::make_shared<Defeat>(std::move(source),name);
}
}
