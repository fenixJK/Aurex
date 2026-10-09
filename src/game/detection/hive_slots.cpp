#include "hive_slots.hpp"
#include <algorithm>

namespace hive::game {
namespace {
class HiveSlots final : public Detector {
public:
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& f=input.frame;
        if (!f.valid() || !input.generation || (input.cancelled && input.cancelled())) return {};
        auto result=std::make_shared<HiveSlotsDetection>();
        result->candidates={Knowledge::unknown,{},f.captured,f.window,input.generation};
        auto& slots=result->candidates.value;
        slots.fill(Knowledge::unknown);
        const double scale=f.width/f.window.bounds.width,scale_y=f.height/f.window.bounds.height;
        if (scale<.5 || scale>4 || scale_y<.5 || scale_y>4 || std::abs(scale-scale_y)>.1
            || f.width>8192 || f.height>8192 || f.window.bounds.width<320 || f.window.bounds.height<200) return result;
        constexpr std::array<double,6> x{.9013,.7007,.4954,.3046,.1021,.0561};
        const double width=f.height*.8,left=(f.width-width)/2,top=f.height*.25;
        const int radius=std::clamp(static_cast<int>(std::lround(f.height/540.0)),1,16);
        if (left-radius<0 || left+width+radius>=f.width) return result;
        for (std::size_t slot=0;slot<slots.size();++slot) {
            if (input.cancelled && input.cancelled()) return {};
            const int cx=static_cast<int>(std::lround(left+width*x[slot]));
            const int cy=static_cast<int>(std::lround(top+(slot==5?f.height*.06985:0)));
            unsigned red=0,opaque=0,total=0;
            for (int py=cy-radius;py<=cy+radius;++py) for (int px=cx-radius;px<=cx+radius;++px) {
                if (px<0 || py<0 || px>=f.width || py>=f.height) return result;
                const auto offset=(static_cast<std::size_t>(py)*f.width+px)*4;
                ++total;
                if (f.rgba[offset+3]<251) continue;
                ++opaque;
                const int r=f.rgba[offset],g=f.rgba[offset+1],b=f.rgba[offset+2];
                if (r>g+7 && r>b+7) ++red;
            }
            if (opaque!=total) continue;
            if (red*5>=total*4) slots[slot]=Knowledge::present;
            else if (red*5<=total) slots[slot]=Knowledge::absent;
        }
        result->candidates.knowledge=Knowledge::present;
        return result;
    }
};
class DetectedSlots final : public HiveSlotsSource {
    std::shared_ptr<const DetectionSource> source_;
public:
    explicit DetectedSlots(std::shared_ptr<const DetectionSource> source):source_(std::move(source)) {}
    Observation<HiveSlotCandidates> read(const ObservationContext& c) const override {
        const auto packet=source_->read();
        if (!packet || !packet->error.empty()) return {};
        const auto& context=packet->context;
        const Observation<bool> envelope{Knowledge::present,true,context.now,context.window,context.generation};
        if (!envelope.has_value(c,Time(2000))) return {};
        const auto* slots=dynamic_cast<const HiveSlotsDetection*>(packet->value.get());
        if (!slots || !slots->candidates.has_value(c,Time(2000)) || slots->candidates.captured!=context.now) return {};
        for (const auto value : slots->candidates.value)
            if (value!=Knowledge::unknown && value!=Knowledge::absent && value!=Knowledge::present) return {};
        return slots->candidates;
    }
};
}
std::shared_ptr<const Detector> hive_slots_detector() { return std::make_shared<HiveSlots>(); }
std::shared_ptr<const HiveSlotsSource> hive_slots_source(std::shared_ptr<const DetectionSource> source) {
    if (!source) throw std::invalid_argument("Hive survey requires a detection source");
    return std::make_shared<DetectedSlots>(std::move(source));
}
}
