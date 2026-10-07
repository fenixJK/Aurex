#include "hive/game/detection/movement_buffs.hpp"
#include <algorithm>
#include "../detection/buff_bar.hpp"

namespace hive::game {
namespace {
struct Effects {
    double multiplier{1},flat{};
    bool operator==(const Effects&) const=default;
};
std::optional<Effects> effects(const MovementBuffCandidates& reading) {
    if (!reading.layout_known || !reading.bar.valid() || reading.candidates.size()>64) return {};
    Effects result;
    std::vector<const MovementBuffCandidate*> haste;
    bool oil=false,smoothie=false,plus=false,bear=false;
    for (const auto& candidate : reading.candidates) {
        const auto& box=candidate.pixels;
        if (!box.valid() || box.x<reading.bar.x || box.y<reading.bar.y
            || box.x+box.width>reading.bar.x+reading.bar.width || box.y+box.height>reading.bar.y+reading.bar.height) return {};
        if (candidate.name=="Haste") { haste.push_back(&candidate); continue; }
        bool* seen=nullptr;
        if (candidate.name=="Oil") seen=&oil;
        else if (candidate.name=="Smoothie") seen=&smoothie;
        else if (candidate.name=="HastePlus") seen=&plus;
        else if (candidate.name=="BearBrown" || candidate.name=="BearBlack" || candidate.name=="BearPanda"
            || candidate.name=="BearPolar" || candidate.name=="BearGummy" || candidate.name=="BearScience" || candidate.name=="BearMother") {
            seen=&bear; result.flat+=4;
        } else return {};
        if (*seen || candidate.stacks) return {};
        *seen=true;
    }
    result.multiplier=(oil?1.2:1)*(smoothie?1.25:1)*(plus?2:1);
    if (haste.size()>2) return {};
    if (!haste.empty()) {
        std::sort(haste.begin(),haste.end(),[](const auto* a,const auto* b) { return a->pixels.x<b->pixels.x; });
        if (!haste.front()->stacks || *haste.front()->stacks<1 || *haste.front()->stacks>10) return {};
        result.multiplier*=1+*haste.front()->stacks*.1;
        if (haste.size()==2) {
            if (haste[0]->pixels.x+haste[0]->pixels.width>haste[1]->pixels.x
                || (haste[1]->stacks && *haste[1]->stacks!=1)) return {};
            result.flat+=10;
        }
    }
    return result;
}
class BuffDetector final : public Detector {
    mutable std::mutex mutex_;
    mutable std::optional<ObservationContext> context_;
    mutable std::optional<detail::BuffBar> layout_;
    mutable int width_{},height_{};
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        std::lock_guard lock(mutex_);
        auto result=std::make_shared<MovementBuffDetection>();
        const auto& frame=input.frame;
        if (!frame.valid() || !input.generation || (input.cancelled && input.cancelled())) return result;
        if (!context_ || context_->generation!=input.generation || context_->window.id!=frame.window.id
            || context_->window.process!=frame.window.process || context_->window.bounds!=frame.window.bounds
            || frame.captured<context_->now) { layout_.reset(); }
        context_=ObservationContext{frame.window,input.generation,frame.captured};
        if (width_!=frame.width || height_!=frame.height) layout_.reset();
        width_=frame.width; height_=frame.height;
        if (const auto layout=detail::hud_buff_bar(input)) layout_=layout;
        if (layout_) result->buffs=detail::movement_buffs_at(input,*layout_);
        return result;
    }
};
double permanent_multiplier(double base) {
    const auto multiple=[](double value,double factor) { return std::abs(value-std::round(value/factor)*factor)<1e-8; };
    if (multiple(base,1.265) || multiple(std::round((base+.005)*1000)/1000,1.265)) return 1.265;
    if (multiple(base,1.1)) return 1.1;
    if (multiple(base,1.15)) return 1.15;
    return 1;
}
class BuffSpeed final : public MovementSpeedSource {
    std::shared_ptr<const DetectionSource> source_;
    std::shared_ptr<const DetectionSource> text_;
    double base_,permanent_;
    mutable std::mutex mutex_;
    mutable std::optional<ObservationContext> context_;
    mutable Time captured_{Time(-1)};
    mutable Observation<double> published_;
public:
    BuffSpeed(std::shared_ptr<const DetectionSource> source,double base,std::shared_ptr<const DetectionSource> text)
        :source_(std::move(source)),text_(std::move(text)),base_(base),permanent_(permanent_multiplier(base)) {
        if (!source_ || !std::isfinite(base_) || base_<8 || base_>200) throw std::invalid_argument("Invalid movement speed source");
    }
    void reset() const override {
        std::lock_guard lock(mutex_);
        context_.reset(); captured_=Time(-1);
        published_={};
    }
    Observation<double> read(const ObservationContext& c,const Observation<double>& measured) const override {
        std::lock_guard lock(mutex_);
        if (!context_ || context_->generation!=c.generation || context_->window.id!=c.window.id
            || context_->window.process!=c.window.process || context_->window.bounds!=c.window.bounds || c.now<context_->now) {
            captured_=Time(-1); published_={};
        }
        context_=c;
        const bool direct=measured.has_value(c,Time(2000)) && std::isfinite(measured.value) && measured.value>=1 && measured.value<=1000;
        const auto fallback=direct?measured:Observation<double>{};
        const auto publish=[&](Observation<double> reading) {
            if (!reading.has_value(c,Time(2000))) return Observation<double>{};
            if (published_.has_value(c,Time(2000)) && reading.captured<=published_.captured) return published_;
            published_=reading; return reading;
        };
        auto packet=source_->read();
        const auto usable=[&](const std::optional<DetectionResult>& result) {
            if (!result || !result->error.empty()) return false;
            const auto* frame=dynamic_cast<const MovementBuffDetection*>(result->value.get());
            const auto& stamp=result->context;
            return frame && frame->buffs.layout_known
                && Observation<bool>{Knowledge::present,true,stamp.now,stamp.window,stamp.generation}.has_value(c,Time(2000));
        };
        if (!usable(packet) && text_) packet=text_->read();
        if (!packet || !packet->error.empty()) return publish(fallback);
        const auto* value=dynamic_cast<const MovementBuffDetection*>(packet->value.get());
        const auto& stamp=packet->context;
        Observation<bool> fresh{Knowledge::present,true,stamp.now,stamp.window,stamp.generation};
        const auto modifiers=value?effects(value->buffs):std::nullopt;
        if (!fresh.has_value(c,Time(2000)) || stamp.now<captured_) return publish(fallback);
        if (!modifiers) {
            return publish(fallback);
        }
        captured_=stamp.now;
        if (direct && measured.captured>=stamp.now) return publish(measured);
        const auto speed=(base_+modifiers->flat*permanent_)*modifiers->multiplier;
        if (!std::isfinite(speed) || speed<1 || speed>1000) return {};
        return publish({Knowledge::present,speed,stamp.now,stamp.window,stamp.generation});
    }
};
}
std::shared_ptr<const Detector> movement_buff_detector() { return std::make_shared<BuffDetector>(); }
std::shared_ptr<const MovementSpeedSource> movement_speed_source(std::shared_ptr<const DetectionSource> source,double base_speed,
    std::shared_ptr<const DetectionSource> text_fallback) {
    return std::make_shared<BuffSpeed>(std::move(source),base_speed,std::move(text_fallback));
}
}
