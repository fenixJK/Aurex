#include "conversation.hpp"
#include "visual_labels.hpp"
#include <algorithm>
#include <array>

namespace hive::game {
namespace {
class ConversationDetector final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& f=input.frame;
        if (!f.valid() || (input.cancelled && input.cancelled())) return {};
        auto result=std::make_shared<ConversationDetection>();
        static constexpr std::array<std::string_view,1> labels{"Click to continue"};
        const auto rows=fixed_ui_labels(f,{.05,.5,.9,.5},labels);
        const TextLine* footer=nullptr;
        for (const auto& line:rows) {
            if (input.cancelled && input.cancelled()) return {};
            const auto r=line.pixels;
            if (!r.valid() || r.x<0 || r.x+r.width>f.width || r.y<f.height*.5 || r.y+r.height>f.height
                || r.height<6 || line.text.size()>80) continue;
            if (line.text!="Click to continue") continue;
            if (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.8f || *line.confidence>1)) return result;
            if (footer) return result;
            footer=&line;
        }
        if (!footer) return result;
        const auto r=footer->pixels;
        const auto blue=[&](int x,int y) {
            if (x<0 || x>=f.width || y<0 || y>=f.height) return false;
            const auto i=(static_cast<std::size_t>(y)*f.width+x)*4;
            return f.rgba[i+2]>=100 && f.rgba[i+2]>f.rgba[i]*1.3 && f.rgba[i+2]>f.rgba[i+1]*1.15;
        };
        int baseline=0;
        for (int y=int(r.y+r.height+1);y<std::min(f.height,int(r.y+r.height*2));++y) {
            const int middle=int(r.x+r.width/2);
            if (!blue(middle,y)) continue;
            int a=middle,b=middle;
            while (a>0 && blue(a-1,y)) --a;
            while (b<f.width && blue(b,y)) ++b;
            if (b-a<r.height*15 || b-a>f.width*.95 || a>r.x || b<r.x+r.width) continue;
            baseline=y; break;
        }
        if (!baseline) return result;
        result->view.visible=true;
        result->view.next=Point{(r.x+r.width/2)/f.width,(r.y+r.height/2)/f.height};
        return result;
    }
};
class Source final : public ConversationSource {
    std::shared_ptr<const DetectionSource> source_;
public:
    explicit Source(std::shared_ptr<const DetectionSource> source):source_(std::move(source)) {}
    Observation<ConversationView> read(const ObservationContext& c) const override {
        const auto result=source_->read(); if (!result) return {};
        if (!result->error.empty()) throw std::runtime_error(result->error);
        const auto* value=dynamic_cast<const ConversationDetection*>(result->value.get());
        if (!value) return {};
        Observation<ConversationView> reading{Knowledge::present,value->view,result->context.now,result->context.window,result->context.generation};
        return reading.has_value(c,Time(2000))?reading:Observation<ConversationView>{};
    }
};
}
std::shared_ptr<const Detector> conversation_detector() { return std::make_shared<ConversationDetector>(); }
std::shared_ptr<const ConversationSource> conversation_source(std::shared_ptr<const DetectionSource> source) {
    if (!source) throw std::invalid_argument("Conversation needs a detection source");
    return std::make_shared<Source>(std::move(source));
}
}
