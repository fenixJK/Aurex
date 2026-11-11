#include "guiding.hpp"
#include "game/data/fields.hpp"
#include <algorithm>
#include <cctype>

namespace hive::game {
namespace {
std::string words(const std::string& text) {
    std::string result;
    for(unsigned char c:text) {
        if(std::isalnum(c)) result+=char(std::tolower(c));
        else if(!result.empty() && result.back()!=' ') result+=' ';
    }
    if(!result.empty() && result.back()==' ') result.pop_back();
    return result;
}
class Guiding final : public Detector {
    mutable ObservationContext previous_;
    mutable std::string candidate_,visible_;
    mutable unsigned samples_{};
    mutable Time absent_since_{};
    mutable std::uint64_t sequence_{};
    mutable std::optional<GuidingActivation> activation_;
public:
    std::vector<Rect> text_regions(const Frame&,std::uint64_t) const override { return {{.5,.5,.5,.5}}; }
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& frame=input.frame;
        if(!frame.valid() || !input.generation || input.text.size()>4096 || (input.cancelled && input.cancelled())) return {};
        if(previous_.generation!=input.generation || previous_.window.id!=frame.window.id
            || previous_.window.process!=frame.window.process || previous_.window.bounds!=frame.window.bounds
            || frame.captured<previous_.now || frame.captured-previous_.now>Time(5000)) {
            candidate_.clear();visible_.clear();samples_=0;activation_.reset();absent_since_={};
        }
        std::string found;
        for(const auto& line:input.text) {
            const auto& box=line.pixels;
            if(line.text.size()>160 || !box.valid() || box.x<frame.width*.5 || box.y<frame.height*.5
                || box.x+box.width>frame.width+.5 || box.y+box.height>frame.height+.5
                || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.8 || *line.confidence>1))) continue;
            const auto caption=words(line.text);
            for(const auto& field:fields()) {
                const auto name=words(field.name);
                const auto prefix="guiding star on the "+name;
                if(caption==prefix || caption==prefix+" field" || caption==prefix+" patch" || caption==prefix+" forest") {
                    found=field.name;break;
                }
            }
            if(!found.empty()) break;
        }
        if(frame.captured>previous_.now) {
            if(found.empty()) {
                candidate_.clear();samples_=0;
                if(absent_since_==Time{}) absent_since_=frame.captured;
                if(frame.captured-absent_since_>=Time(3000)) visible_.clear();
            } else {
                absent_since_={};
                if(candidate_!=found) { candidate_=found;samples_=0; }
                if(++samples_>=2 && visible_!=found) {
                    // A toast can stay visible or be rediscovered after a dropped frame.
                    // The ability cannot activate again during its ten-minute lifetime.
                    if(!activation_ || frame.captured-activation_->seen>=guiding_star_duration)
                        activation_=GuidingActivation{found,frame.captured,++sequence_};
                    visible_=found;
                }
            }
        }
        previous_={frame.window,input.generation,frame.captured};
        auto result=std::make_shared<GuidingDetection>();result->activation=activation_;return result;
    }
};
}
std::shared_ptr<const Detector> guiding_detector() { return std::make_shared<Guiding>(); }
}
