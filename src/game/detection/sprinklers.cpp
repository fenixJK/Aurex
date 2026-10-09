#include "sprinklers.hpp"
#include <algorithm>
#include <cctype>

namespace hive::game {
std::span<const SprinklerEquipment> sprinkler_equipment() {
    static const SprinklerEquipment values[]{{"None",0,false},{"Basic",1,false},{"Silver",2,true},
        {"Golden",3,true},{"Diamond",4,true},{"Supreme",1,false}};
    return values;
}
Observation<SprinklerFeedback> read_sprinkler_feedback(const ObservationContext& c,const State& state,Time minimum) {
    const auto& snapshot=state.text;
    Observation<SprinklerFeedback> result{Knowledge::unknown,{},snapshot.captured,snapshot.window,snapshot.generation};
    if (!snapshot.has_value(c,Time(2000),minimum) || snapshot.value.width<=0 || snapshot.value.height<=0
        || snapshot.value.lines.empty() || snapshot.value.lines.size()>2048) return result;
    auto normalized=[](const std::string& text) {
        std::string value=" ";
        for (unsigned char c : text) {
            if (std::isalnum(c)) value+=static_cast<char>(std::tolower(c));
            else if (value.back()!=' ') value+=' ';
        }
        return value+" ";
    };
    std::vector<const TextLine*> lines;
    const auto width=snapshot.value.width,height=snapshot.value.height;
    for (const auto& line : snapshot.value.lines) {
        const auto& box=line.pixels;
        if (!box.valid() || box.x<0 || box.y<0 || box.x+box.width>width || box.y+box.height>height || line.text.size()>512) {
            if (line.text.size()<=512 && normalized(line.text).find("sprinkler")!=std::string::npos) return result;
            continue;
        }
        if (box.x+box.width<width*.55 || box.y+box.height<height*.5) continue;
        lines.push_back(&line);
    }
    if (lines.size()>128) return result;
    const auto confident=[](const TextLine& line) {
        return !line.confidence || (std::isfinite(*line.confidence) && *line.confidence>=.8f && *line.confidence<=1);
    };
    std::optional<SprinklerFeedback> feedback;
    for (const auto* line : lines) {
        std::string text=line->text;
        bool certain=confident(*line);
        for (const auto* other : lines) {
            if (line==other) continue;
            const auto& a=line->pixels; const auto& b=other->pixels;
            if (b.y>=a.y+a.height*.8 && b.y<=a.y+a.height*2.5 && b.height>=a.height*.6 && b.height<=a.height*1.6
                && std::abs(a.x-b.x)<=a.height*2) { text+=" "+other->text; certain=certain && confident(*other); }
        }
        text=normalized(text);
        if (text.find(" sprinkler ")==std::string::npos && text.find(" sprinklers ")==std::string::npos) continue;
        if (!certain) return result;
        const bool standing=text.find(" standing ")!=std::string::npos && text.find(" must ")!=std::string::npos;
        const bool close=text.find(" this close ")!=std::string::npos;
        if (standing && close) return result;
        const bool rejected=text.find(" can t ")!=std::string::npos || text.find(" cannot ")!=std::string::npos || text.find(" must ")!=std::string::npos;
        if (!standing && !close && !rejected) continue;
        const auto found=standing?SprinklerFeedback::too_high:close?SprinklerFeedback::too_low:SprinklerFeedback::unavailable;
        if (feedback && *feedback!=found) return result;
        feedback=found;
    }
    result.knowledge=Knowledge::present; result.value=feedback.value_or(SprinklerFeedback::none);
    return result;
}
namespace {
class DetectedSprinklers final : public SprinklerSource {
    std::shared_ptr<const DetectionSource> source_;
public:
    explicit DetectedSprinklers(std::shared_ptr<const DetectionSource> source):source_(std::move(source)) {}
    Observation<SprinklerSite> read(const ObservationContext&,const State&) const override {
        const auto result=source_->read();
        if (!result || !result->error.empty()) return {};
        const auto detected=std::dynamic_pointer_cast<const SprinklerDetection>(result->value);
        if (!detected) return {};
        const auto& context=result->context;
        return {Knowledge::present,detected->site,context.now,context.window,context.generation};
    }
};
}
std::shared_ptr<const SprinklerSource> sprinkler_source(std::shared_ptr<const DetectionSource> source) {
    if (!source) throw std::invalid_argument("Sprinkler detection source is missing");
    return std::make_shared<DetectedSprinklers>(std::move(source));
}
}
