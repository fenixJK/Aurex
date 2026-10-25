#include "respawn.hpp"
#include "visual_labels.hpp"
#include "landmarks.hpp"
#include "perception.hpp"
#include "connection.hpp"
#include <algorithm>
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
Observation<RespawnView> read_respawn_menu(const Frame& f,std::uint64_t generation) {
    Observation<RespawnView> result{Knowledge::unknown,{},f.captured,f.window,generation};
    if (!f.valid() || !f.window.id || !generation) return result;
    // The HUD can remain readable behind Roblox's translucent menu.
    // Resolve its controls before accepting the underlying game view.
    static constexpr std::array<std::string_view,7> controls{"Reset","Respawn","Cancel","Don't Respawn","Reset Character","Resume","Leave"};
    const auto labels=fixed_ui_labels(f,{0,.2,1,.8},controls);
    const TextLine *reset=nullptr,*cancel=nullptr,*leave=nullptr,*resume=nullptr;
    for (const auto& label:labels) {
        const TextLine** target=label.text=="Cancel" || label.text=="Don't Respawn"?&cancel:
            label.text=="Leave"?&leave:label.text=="Resume"?&resume:&reset;
        if (*target) return result;
        *target=&label;
    }
    auto aligned=[](const Rect& a,const Rect& b) {
        return std::abs(a.y+a.height/2-b.y-b.height/2)<std::max(a.height,b.height)*.6;
    };
    if (reset && cancel) {
        const auto a=reset->pixels,b=cancel->pixels;
        if (aligned(a,b) && std::abs(a.x-b.x)>std::max(a.width,b.width)) {
            result.knowledge=Knowledge::present;
            result.value={RespawnScreen::confirmation,Point{(a.x+a.width/2)/f.width,(a.y+a.height/2)/f.height}};
        }
        return result;
    }
    if (reset && leave) {
        const auto a=reset->pixels,b=leave->pixels;
        const double height=std::max(a.height,b.height);
        const bool horizontal=aligned(a,b) && b.x<a.x
            && (!resume || (aligned(a,resume->pixels) && a.x<resume->pixels.x));
        // Roblox also places these shortcuts in a vertical sidebar.
        const bool vertical=std::abs(a.x-b.x)<=height*1.5 && a.x+a.width<f.width*.5
            && b.y-a.y>=height && b.y-a.y<=height*6;
        if (a.y>f.height*.6 && (horizontal || vertical)) {
            result.knowledge=Knowledge::present;
            result.value={RespawnScreen::menu,{}};
            return result;
        }
    }
    result.knowledge=Knowledge::absent;
    return result;
}
namespace {
struct Packet final : Detection { Observation<RespawnView> view; };
bool visible_game(const DetectionInput& input) {
    return (game_hud_visible(input.frame,input.generation) || interaction_prompt_region(input.frame).has_value())
        && connection_dialog_regions(input.frame).empty();
}
class RespawnDetector final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& f=input.frame;
        if (!f.valid() || !f.window.id || !input.generation || (input.cancelled && input.cancelled())) return {};
        auto packet=std::make_shared<Packet>();
        packet->view={Knowledge::unknown,{},f.captured,f.window,input.generation};
        const auto health=read_empty_health_bar(f,input.generation);
        const auto menu=read_respawn_menu(f,input.generation);
        if (menu.knowledge!=Knowledge::absent) { packet->view=menu; return packet; }
        if (visible_game(input)) {
            packet->view.knowledge=Knowledge::present;
            packet->view.value={health.value?RespawnScreen::respawning:RespawnScreen::game,{}};
        } else if (health.knowledge==Knowledge::present && health.value) {
            packet->view.knowledge=Knowledge::present;
            packet->view.value={RespawnScreen::respawning,{}};
        }
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
