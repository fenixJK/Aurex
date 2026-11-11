#include "game/detection/movement_buffs.hpp"
#include <algorithm>
#include "game/detection/buff_bar.hpp"
#include "game/detection/buff_catalog.hpp"
#include "game/detection/perception.hpp"

namespace hive::game {
namespace {
struct Effects {
    double multiplier{1},flat{};
    bool operator==(const Effects&) const=default;
};
std::optional<Effects> effects(const MovementBuffCandidates& reading) {
    if (!reading.layout_known || !reading.bar.valid() || reading.candidates.size()>64) return {};
    Effects result;
    const auto definitions=detail::movement_buff_definitions();
    std::vector<std::vector<const MovementBuffCandidate*>> groups(definitions.size());
    for (const auto& candidate:reading.candidates) {
        const auto& box=candidate.pixels;
        if (!box.valid() || box.x<reading.bar.x || box.y<reading.bar.y
            || box.x+box.width>reading.bar.x+reading.bar.width || box.y+box.height>reading.bar.y+reading.bar.height) return {};
        const auto definition=std::find_if(definitions.begin(),definitions.end(),[&](const auto& d){return d.type.name==candidate.name;});
        if (definition==definitions.end()) return {};
        groups[definition-definitions.begin()].push_back(&candidate);
    }
    for (std::size_t i=0;i<definitions.size();++i) {
        auto& candidates=groups[i];const auto& definition=definitions[i];
        if (candidates.size()>definition.movement.size()) return {};
        std::sort(candidates.begin(),candidates.end(),[](const auto* a,const auto* b){return a->pixels.x<b->pixels.x;});
        for (std::size_t j=0;j<candidates.size();++j) {
            const auto& candidate=*candidates[j];const auto& effect=definition.movement[j];
            if (j && candidates[j-1]->pixels.x+candidates[j-1]->pixels.width>candidate.pixels.x) return {};
            if (effect.maximum_stacks) {
                if (candidate.stacks ? *candidate.stacks<1 || *candidate.stacks>effect.maximum_stacks : !effect.unknown_stacks_allowed) return {};
            } else if (candidate.stacks) return {};
            result.multiplier*=effect.multiplier+effect.per_stack*candidate.stacks.value_or(0);
            result.flat+=effect.flat;
        }
    }
    return result;
}
class BuffDetector final : public Detector {
    mutable std::mutex mutex_;
    mutable std::optional<ObservationContext> context_;
    mutable std::optional<detail::BuffBar> layout_;
    mutable int width_{},height_{};
    mutable std::vector<std::string> previous_icons_;
    mutable bool disappearance_pending_{};
public:
    Rect capture_region() const override { return upper_hud_region; }
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        std::lock_guard lock(mutex_);
        auto result=std::make_shared<MovementBuffDetection>();
        const auto& frame=input.frame;
        if (!frame.valid() || !input.generation || (input.cancelled && input.cancelled())) return result;
        const bool reset=!context_ || context_->generation!=input.generation || context_->window.id!=frame.window.id
            || context_->window.process!=frame.window.process || context_->window.bounds!=frame.window.bounds
            || frame.captured<context_->now || width_!=frame.width || height_!=frame.viewport_height();
        const bool fresh=reset || frame.captured>context_->now;
        if(reset) {layout_.reset();previous_icons_.clear();disappearance_pending_=false;}
        context_=ObservationContext{frame.window,input.generation,frame.captured};
        width_=frame.width; height_=frame.viewport_height();
        if (const auto layout=detail::hud_buff_bar(input)) layout_=layout;
        if (layout_) result->buffs=detail::movement_buffs_at(input,*layout_);
        if(fresh && result->buffs.layout_known) {
            std::vector<std::string> current;
            for(const auto& candidate:result->buffs.candidates)current.push_back(candidate.name);
            std::sort(current.begin(),current.end());
            // Roblox slides the remaining tiles when one expires. A single
            // missed tile during that animation is not proof of lower speed.
            // Require another fresh frame; repeated reads cannot confirm it.
            disappearance_pending_=!std::includes(current.begin(),current.end(),previous_icons_.begin(),previous_icons_.end());
            previous_icons_=std::move(current);
        }
        if(disappearance_pending_)result->buffs.layout_known=false;
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
    double base_,permanent_;
    mutable std::mutex mutex_;
    mutable std::optional<ObservationContext> context_;
    mutable Time captured_{Time(-1)};
    mutable Observation<double> published_;
public:
    BuffSpeed(std::shared_ptr<const DetectionSource> source,double base)
        :source_(std::move(source)),base_(base),permanent_(permanent_multiplier(base)) {
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
std::shared_ptr<const MovementSpeedSource> movement_speed_source(std::shared_ptr<const DetectionSource> source,double base_speed) {
    return std::make_shared<BuffSpeed>(std::move(source),base_speed);
}
}
