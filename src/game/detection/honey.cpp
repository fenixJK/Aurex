#include "honey.hpp"
#include "hud_numbers.hpp"
#include <cmath>
namespace hive::game {
using namespace detail;
namespace {
std::optional<std::uint64_t> honey_amount(std::string text) {
    text=normalize(text);
    if (text.starts_with("honey ")) text.erase(0,6);
    // OCR may join the total, its smaller (+.../sec) caption and even the
    // adjacent Pollen label. Only the leading balance belongs to this counter.
    auto end=text.find_first_of("(+");
    if (const auto pollen=text.find(" pollen");pollen!=std::string::npos) end=std::min(end,pollen);
    if (end!=std::string::npos) text.resize(end);
    text.erase(text.find_last_not_of(' ')+1);
    std::replace(text.begin(),text.end(),'.',',');
    // Some engines read thousands separators as spaces. Keep the same strict
    // three-digit grouping validation rather than concatenating arbitrary digits.
    std::replace(text.begin(),text.end(),' ',',');
    return amount(text,1000000000000000000ULL);
}
Reading<std::uint64_t> honey_reading(const Frame& frame,std::span<const TextLine> lines,std::uint64_t generation,
    const std::optional<std::array<Rect,2>>& resources) {
    Reading<std::uint64_t> result{Knowledge::unknown,{},frame.captured,frame.window,generation};
    if (!frame.valid() || !resources) return result;
    const auto& region=resources->front();
    std::optional<std::uint64_t> found;
    for (const auto& line:lines) {
        const auto& r=line.pixels; const auto text=normalize(line.text);
        if (!inside(r,frame) || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.5f || *line.confidence>1))) continue;
        const auto x=(r.x-region.x)/region.width,y=r.y+r.height/2;
        // The paired HUD boxes establish which counter this is even when OCR
        // misses the small Honey label. The rate caption starts farther right.
        if (x<0 || x>=.5 || y<region.y || y>region.y+region.height || (!text.starts_with("honey ") && x<.15)) continue;
        if (const auto value=honey_amount(text)) {
            if (found && *found!=*value) return result;
            found=value;
        }
    }
    if (found) { result.value=*found; result.knowledge=Knowledge::present; }
    return result;
}
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
        result->amount=honey_reading(input.frame,input.text,input.generation,resource_hud_regions(input));
        return result;
    }
};
}
Reading<std::uint64_t> read_honey(const Frame& frame,const std::vector<TextLine>& lines,std::uint64_t generation) {
    return honey_reading(frame,lines,generation,resource_hud_regions(frame));
}
std::shared_ptr<const Detector> honey_detector() { return std::make_shared<HoneyDetector>(); }
}
