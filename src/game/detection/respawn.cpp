#include "respawn.hpp"
#include "dialog.hpp"
#include <algorithm>
#include <set>
#include <cctype>
#include <array>

namespace hive::game {
Observation<bool> read_empty_health_bar(const Frame& frame,std::uint64_t generation) {
    Observation<bool> result{Knowledge::unknown,false,frame.captured,frame.window,generation};
    if (!frame.valid() || !frame.window.bounds.valid() || !generation || frame.window.bounds.width<300) return result;
    const double scale=frame.width/frame.window.bounds.width;
    if (scale<.5 || scale>4 || frame.height<80*scale) return result;
    auto color=[&](double from_right,double y) {
        const int x=frame.width-1-int(std::round(from_right*scale)),row=int(std::round(y*scale));
        const auto index=(std::size_t(row)*frame.width+x)*4;
        return std::array<int,4>{frame.rgba[index],frame.rgba[index+1],frame.rgba[index+2],frame.rgba[index+3]};
    };
    result.knowledge=Knowledge::present;
    for (int y=30;y<=66;++y) for (int offset=-3;offset<=3;++offset) {
        const auto fill=color(76+offset,y);
        const auto [lo,hi]=std::minmax({fill[0],fill[1],fill[2]});
        if (fill[3]<250 || lo<40 || hi>180 || hi-lo>65) continue;
        auto same=[&](const auto& c) {
            return c[3]>=250 && std::abs(c[0]-fill[0])<=12 && std::abs(c[1]-fill[1])<=12 && std::abs(c[2]-fill[2])<=12;
        };
        auto border=[&](const auto& c) {
            return c[3]>=250 && c[0]+c[1]+c[2]+36<fill[0]+fill[1]+fill[2];
        };
        bool empty=true;
        for (int x:{25,45,76,105,132}) {
            if (!same(color(x+offset,y)) || !same(color(x+offset,y-3)) || !same(color(x+offset,y+3))
                || !border(color(x+offset,y-8)) || !border(color(x+offset,y+8))) { empty=false; break; }
        }
        if (empty && border(color(139+offset,y)) && border(color(17+offset,y))) {
            result.value=true; return result;
        }
    }
    return result;
}
namespace {
std::string normalized(const std::string& text) {
    std::string result;
    for (unsigned char c : text) {
        if (std::isspace(c)) { if (!result.empty() && result.back()!=' ') result+=' '; }
        else result+=static_cast<char>(std::tolower(c));
    }
    if (!result.empty() && result.back()==' ') result.pop_back();
    return result;
}
struct Packet final : Detection { Observation<RespawnView> view; };
class RespawnDetector final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& f=input.frame;
        if (!f.valid() || !f.window.id || !input.generation || input.text.size()>2048 || (input.cancelled && input.cancelled())) return {};
        auto packet=std::make_shared<Packet>();
        packet->view={Knowledge::unknown,{},f.captured,f.window,input.generation};
        const ObservationContext context{f.window,input.generation,f.captured};
        const Reading<TextSnapshot> text{Knowledge::present,{f.width,f.height,{input.text.begin(),input.text.end()}},f.captured,f.window,input.generation};
        const PromptLandmark question{{"reset your character","want to respawn"},{.05,.05,.9,.9},.65f,PromptMatch::phrase,true};
        auto dialog=read_dialog(context,text,question,{"Reset","Cancel"});
        if (dialog.knowledge!=Knowledge::present) {
            auto centered=question; centered.region={.2,.15,.6,.7};
            dialog=read_dialog(context,text,centered,{"Respawn","Don't Respawn"});
        }
        if (dialog.knowledge==Knowledge::present) {
            packet->view.knowledge=Knowledge::present; packet->view.value={RespawnScreen::confirmation,dialog.value.yes}; return packet;
        }
        const TextLine* pause_reset=nullptr;
        bool leave{},resume{};
        for (const auto& line:input.text) {
            const auto& r=line.pixels;
            if (!r.valid() || r.y<f.height*.65 || r.y+r.height>f.height*.96
                || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.65f || *line.confidence>1))) continue;
            const auto name=normalized(line.text);
            const auto x=(r.x+r.width/2)/f.width;
            if (x>.2 && x<.45 && (name=="leave" || name=="l leave")) leave=true;
            if (x>.55 && x<.8 && (name=="resume" || name=="esc resume")) resume=true;
            if (x>.4 && x<.6 && (name=="respawn" || name=="r respawn" || name=="reset character" || name=="r reset character")) {
                if (pause_reset) return packet;
                pause_reset=&line;
            }
        }
        if (pause_reset && leave && resume) {
            packet->view.knowledge=Knowledge::present;
            packet->view.value={RespawnScreen::menu,{}};
            return packet;
        }
        const auto health=read_empty_health_bar(f,input.generation);
        if (health.knowledge==Knowledge::present && health.value) {
            packet->view.knowledge=Knowledge::present;
            packet->view.value={RespawnScreen::respawning,{}}; return packet;
        }
        const TextLine* respawn=nullptr;
        std::set<std::string> neighbors; unsigned matches{}; bool honey{},pollen{},modal{};
        for (const auto& line : input.text) {
            const auto& r=line.pixels;
            if (!r.valid() || r.x<0 || r.y<0 || r.x+r.width>f.width || r.y+r.height>f.height || line.text.size()>512) continue;
            const auto name=normalized(line.text);
            if (name.find("reset your character")!=std::string::npos || name.find("sure you want")!=std::string::npos || name=="cancel") modal=true;
            const auto x=(r.x+r.width/2)/f.width,y=(r.y+r.height/2)/f.height;
            if (x>=.2 && x<=.8 && y>=.2 && y<=.85
                && (name=="disconnected" || name=="connection error" || name=="reconnect")) modal=true;
            if (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.5f || *line.confidence>1)) continue;
            honey=honey || (r.y<f.height*.15 && (name=="honey" || name.starts_with("honey ")));
            pollen=pollen || (r.y<f.height*.15 && (name=="pollen" || name.starts_with("pollen ")));
            if (line.confidence && *line.confidence<.65f) continue;
            if (name=="respawn" || name=="r respawn" || name=="reset character" || name=="r reset character") { respawn=&line; ++matches; }
        }
        if (modal) return packet;
        if (matches==1) for (const auto& line : input.text) {
            const auto name=normalized(line.text); const auto& r=line.pixels;
            if (!r.valid() || r.x<0 || r.y<0 || r.x+r.width>f.width || r.y+r.height>f.height
                || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.65f || *line.confidence>1))) continue;
            static constexpr std::string_view labels[]{"report abuse","leaderboard","inventory","emotes","resume","leave","settings",
                "friends chat","shop","shop new","self view","switch avatar","music","captures","report"};
            if (std::find(std::begin(labels),std::end(labels),name)==std::end(labels)) continue;
            const auto& a=respawn->pixels;
            if (std::abs(r.x-a.x)<std::max(a.width,r.width)*2 && std::abs(r.y-a.y)<f.height*.7) neighbors.insert(name);
        }
        if (matches==1 && neighbors.size()>=2) {
            const auto& r=respawn->pixels;
            packet->view.knowledge=Knowledge::present;
            packet->view.value={RespawnScreen::menu,Point{(r.x+r.width/2)/f.width,(r.y+r.height/2)/f.height}};
        } else if (!matches) {
            const auto hive=read_prompt_landmark(context,text,hive_proximity_prompt());
            if ((honey && pollen) || (hive.knowledge==Knowledge::present && hive.value)) {
                packet->view.knowledge=Knowledge::present;
                packet->view.value={RespawnScreen::game,{}};
            }
        }
        if (input.cancelled && input.cancelled()) return {};
        return packet;
    }
};
class ObservedRespawn final : public RespawnSource {
    std::shared_ptr<const DetectionSource> source_;
public:
    explicit ObservedRespawn(std::shared_ptr<const DetectionSource> source):source_(std::move(source)) {}
    Observation<RespawnView> read(const ObservationContext& context) const override {
        const auto reading=source_->read();
        if (!reading || !reading->error.empty()) return {};
        const auto packet=std::dynamic_pointer_cast<const Packet>(reading->value);
        if (!packet || packet->view.captured!=reading->context.now || !packet->view.has_value(context,Time(2000))
            || packet->view.window.id!=reading->context.window.id || packet->view.window.process!=reading->context.window.process
            || packet->view.window.bounds!=reading->context.window.bounds || packet->view.generation!=reading->context.generation) return {};
        return packet->view;
    }
};
}
std::shared_ptr<const Detector> respawn_detector() { return std::make_shared<RespawnDetector>(); }
std::shared_ptr<const RespawnSource> respawn_source(std::shared_ptr<const DetectionSource> source) {
    if (!source) throw std::invalid_argument("Respawn observations need a detection source");
    return std::make_shared<ObservedRespawn>(std::move(source));
}
}
