#include "honey.hpp"
#include "hud_numbers.hpp"
#include <cmath>
namespace hive::game {
using namespace detail;
Reading<std::uint64_t> read_honey(const Frame& frame,const std::vector<TextLine>& lines,std::uint64_t generation) {
    Reading<std::uint64_t> result{Knowledge::unknown,{},frame.captured,frame.window,generation};
    if (!frame.valid()) return result;
    std::optional<std::uint64_t> found;
    const auto number=[](std::string value) {
        value.erase(0,value.find_first_not_of(" \t")); value.erase(value.find_last_not_of(" \t")+1);
        std::replace(value.begin(),value.end(),'.',',');
        return amount(value,1000000000000000000ULL);
    };
    const auto credible=[](const TextLine& line) { return !line.confidence || (std::isfinite(*line.confidence) && *line.confidence>=.5f && *line.confidence<=1); };
    for (const auto& label:lines) {
        const auto text=normalize(label.text); const auto& r=label.pixels;
        if (!credible(label) || !inside(r,frame) || r.y+r.height>frame.height*.18 || (text!="honey" && !text.starts_with("honey "))) continue;
        std::vector<std::uint64_t> candidates;
        if (text.starts_with("honey ")) { if (auto value=number(text.substr(6))) candidates.push_back(*value); }
        else for (const auto& candidate:lines) {
            const auto& c=candidate.pixels;
            if (!credible(candidate) || !inside(c,frame) || c.x<r.x+r.width-2 || c.x-(r.x+r.width)>r.height*3
                || std::abs((c.y+c.height/2)-(r.y+r.height/2))>std::max(r.height,c.height)*.65) continue;
            if (auto value=number(candidate.text)) candidates.push_back(*value);
        }
        for (const auto value:candidates) { if (found && *found!=value) return result; found=value; }
    }
    if (found) { result.value=*found; result.knowledge=Knowledge::present; }
    return result;
}
namespace {
class HoneyDetector final : public Detector {
public:
    std::vector<Rect> text_regions(const DetectionInput& input) const override {
        const auto resources=resource_hud_regions(input);
        if (!resources) return {};
        const auto& r=resources->front();
        return {{r.x/input.frame.width,r.y/input.frame.height,r.width/input.frame.width,r.height/input.frame.height}};
    }
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        auto result=std::make_shared<HoneyDetection>();
        result->amount=read_honey(input.frame,{input.text.begin(),input.text.end()},input.generation);
        return result;
    }
};
}
std::shared_ptr<const Detector> honey_detector() { return std::make_shared<HoneyDetector>(); }
}
