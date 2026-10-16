#include "localization.hpp"
#include "game/data/stations.hpp"
#include "game/data/npcs.hpp"
#include <algorithm>
#include <set>

namespace hive::game {
namespace {
constexpr Time freshness{2000};
class InteractionPromptDetector final : public Detector {
    std::string label_;
public:
    explicit InteractionPromptDetector(std::string label):label_(std::move(label)) {}
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        if (!input.frame.valid() || !input.generation || (input.cancelled && input.cancelled())) return {};
        const auto& frame=input.frame;
        const auto region=interaction_prompt_region(frame);
        const bool visible=region && (label_.empty() || interaction_label(interaction_text_image(frame,*region),label_).has_value());
        auto result=std::make_shared<Landmarks>();
        result->readings.emplace("interaction.prompt",Observation<bool>{visible?Knowledge::present:Knowledge::absent,
            visible,frame.captured,frame.window,input.generation});
        return result;
    }
};
class DetectedLandmark final : public LandmarkSource {
    std::shared_ptr<const DetectionSource> source_;
    std::string id_;
public:
    DetectedLandmark(std::shared_ptr<const DetectionSource> source,std::string id):source_(std::move(source)),id_(std::move(id)) {}
    Observation<bool> read(const ObservationContext& c) const override {
        const auto packet=source_->read();
        if (!packet || !packet->error.empty()) return {};
        const auto& context=packet->context;
        const Observation<bool> envelope{Knowledge::present,true,context.now,context.window,context.generation};
        if (!envelope.has_value(c,freshness)) return {};
        const auto* value=dynamic_cast<const Landmarks*>(packet->value.get());
        if (!value || value->readings.size()>32) return {};
        const auto it=value->readings.find(id_);
        if (it==value->readings.end() || it->second.captured!=context.now || !it->second.usable(c,freshness)) return {};
        return it->second;
    }
};
class PromptDetector final : public Detector {
    std::vector<PromptAnchor> anchors_;
    std::vector<std::pair<std::string,std::string>> specific_names_;
public:
    explicit PromptDetector(std::vector<PromptAnchor> anchors):anchors_(std::move(anchors)) {
        if (anchors_.empty() || anchors_.size()>32) throw std::invalid_argument("Invalid landmark catalog size");
        std::set<std::string> ids;
        for (const auto& anchor : anchors_) {
            if (anchor.id.empty() || anchor.id.size()>80 || !ids.insert(anchor.id).second
                || std::any_of(anchor.id.begin(),anchor.id.end(),[](unsigned char c) { return c<33 || c>126; }))
                throw std::invalid_argument("Invalid landmark identity");
            std::visit([](const auto& target) { validate(target); },anchor.prompt);
            if (anchor.status) validate(*anchor.status);
        }
        for (const auto& anchor:anchors_) {
            const auto* target=std::get_if<TextTarget>(&anchor.prompt);
            if (!target || !target->station_name) continue;
            for (const auto& other:anchors_) {
                const auto* specific=std::get_if<TextTarget>(&other.prompt);
                if (specific && specific->station_name && specific->region==target->region
                    && specific->words.ends_with(" "+target->words)) specific_names_.emplace_back(anchor.id,other.id);
            }
        }
    }
    std::vector<Rect> text_regions(const Frame&,std::uint64_t) const override {
        std::vector<Rect> regions;
        for (const auto& anchor:anchors_) {
            const auto add=[&](Rect region) { if (std::find(regions.begin(),regions.end(),region)==regions.end()) regions.push_back(region); };
            add(std::visit([](const auto& target) { return target.region; },anchor.prompt));
            if (anchor.status) add(anchor.status->region);
        }
        return regions;
    }
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        if (!input.frame.valid() || !input.generation || (input.cancelled && input.cancelled())) return {};
        if (input.text.size()>4096) throw std::invalid_argument("Landmark OCR exceeds limits");
        const auto& f=input.frame;
        const ObservationContext context{f.window,input.generation,f.captured};
        const Reading<TextSnapshot> text{input.text.empty()?Knowledge::unknown:Knowledge::present,
            {f.width,f.height,{input.text.begin(),input.text.end()}},f.captured,f.window,input.generation};
        auto result=std::make_shared<Landmarks>();
        State state; state.text=text;
        for (const auto& anchor : anchors_) {
            if (input.cancelled && input.cancelled()) return {};
            auto reading=std::visit([&](const auto& target) {
                if constexpr (std::is_same_v<std::decay_t<decltype(target)>,TextTarget>) return read_text_target(context,state,target);
                else return read_prompt_landmark(context,text,target);
            },anchor.prompt);
            if (anchor.status && !reading.value) {
                const auto status=read_text_target(context,state,*anchor.status);
                if (status.knowledge==Knowledge::present) reading=status;
                else if (status.knowledge==Knowledge::unknown) reading.knowledge=Knowledge::unknown;
            }
            result->readings.emplace(anchor.id,std::move(reading));
        }
        for (const auto& [generic,specific]:specific_names_) {
            auto& reading=result->readings.at(generic);
            if (reading.value && result->readings.at(specific).value) {
                reading.knowledge=Knowledge::absent; reading.value=false;
            }
        }
        return result;
    }
};
bool same(const ObservationContext& a,const ObservationContext& b) {
    return a.generation==b.generation && a.window.id==b.window.id && a.window.process==b.window.process && a.window.bounds==b.window.bounds;
}
}
std::shared_ptr<const LandmarkSource> landmark_source(std::shared_ptr<const DetectionSource> source,std::string id) {
    if (!source || id.empty() || id.size()>80) throw std::invalid_argument("Invalid landmark source");
    return std::make_shared<DetectedLandmark>(std::move(source),std::move(id));
}
std::vector<PromptAnchor> interaction_anchors() {
    std::vector<PromptAnchor> result{{"hive.proximity",hive_proximity_prompt()},
        {"transport.red-cannon",PromptLandmark{{"Fire the Red Cannon","Fire Red Cannon","Red Cannon"},{.2,0,.6,.3},.5f,PromptMatch::exact,true}}};
    result.push_back({"challenge.ant",PromptLandmark{{"Spend 1 Ant Pass to enter the Ant Field."}}});
    result.push_back({"station.ant-pass-paid",PromptLandmark{{"Spend 10 Tickets to Buy 1 Ant Pass","Purchase 1 Ant Pass for 10 Tickets",
        "Buy an Ant Pass for 10 Tickets","Spend 10 Tickets for an Ant Pass"},{.2,0,.6,.3},.5f,PromptMatch::phrase,true}});
    for (const auto& npc : npcs()) result.push_back({npc.location(),npc.prompt()});
    for (const auto& station : stations()) {
        result.push_back({station.location,station_target(station.name),station_target(station.name,true)});
        if (station.name=="Free Ant Pass Dispenser") result.back().prompt=PromptLandmark{{"Free Ant Pass Dispenser","You already have 10 Ant Passes","You already have 10/10 Ant Passes"},{.2,0,.6,.3},.7f,PromptMatch::phrase,true};
    }
    return result;
}
std::shared_ptr<const Detector> landmark_detector(std::vector<PromptAnchor> anchors) { return std::make_shared<PromptDetector>(std::move(anchors)); }
std::shared_ptr<const Detector> interaction_prompt_detector(std::string label) { return std::make_shared<InteractionPromptDetector>(std::move(label)); }
void Localization::invalidate(const ObservationContext& c) {
    context_=c; last_=processed_=c.now; confirmed_={};
}
Observation<std::string> Localization::read(const ObservationContext& c,const std::optional<DetectionResult>& result) {
    if (!c.generation || !c.window.id || !c.window.bounds.valid() || c.now<Time::zero()) throw std::invalid_argument("Invalid localization context");
    if (!same(c,context_) || c.now<last_) { confirmed_={}; processed_=Time(-1); }
    context_=c; last_=c.now;
    auto unknown=[&] { confirmed_={}; return Observation<std::string>{}; };
    if (!result || !result->error.empty() || !same(c,result->context) || result->context.now>c.now
        || c.now-result->context.now>freshness) return unknown();
    const auto* landmarks=dynamic_cast<const Landmarks*>(result->value.get());
    if (!landmarks || landmarks->readings.size()>32) return unknown();
    if (result->context.now<processed_) return unknown();
    if (result->context.now==processed_) return confirmed_.has_value(c,freshness)?confirmed_:Observation<std::string>{};
    processed_=result->context.now;
    const Observation<bool>* match=nullptr; std::string id;
    for (const auto& [name,reading] : landmarks->readings) {
        if (reading.knowledge==Knowledge::present && reading.value) {
            if (!reading.usable(c,freshness) || reading.captured!=result->context.now) return unknown();
            if (match) return unknown();
            match=&reading; id=name;
        }
    }
    if (!match) return unknown();
    confirmed_={Knowledge::present,id,match->captured,match->window,match->generation};
    return confirmed_;
}
}
