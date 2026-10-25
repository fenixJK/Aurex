#include "guards.hpp"
#include <array>
#include <numeric>

namespace hive::automation {
std::optional<double> backpack_reading(const ObservationContext& c,const game::State& state,Time minimum) {
    const auto& reading=state.backpack_percent;
    if (reading.has_value(c,Time(2000),minimum) && std::isfinite(reading.value) && reading.value>=0 && reading.value<=100)
        return reading.value;
    return {};
}
namespace {
class ObservationGuard final : public RunGuard {
    Time seen_{Time(-1)};
    std::optional<double> limit_;
    std::array<double,6> samples_{};
    std::size_t count_{},next_{};
    Time sampled_{Time(-1)},sampled_at_{Time(-1)};
    bool gathering_{};
public:
    ObservationGuard(const RunConfig& config,WorkActivity activity):limit_(config.backpack_limit),gathering_(activity==WorkActivity::gathering) {}
    game::ObservationNeeds observation_needs() const override { return limit_?game::ObservationNeeds{game::ObservationKind::backpack}:game::ObservationNeeds{}; }
    GuardDecision evaluate(const View& view,Time started) override {
        const auto& c=view.context;
        const auto* state=view.observations;
        if (limit_) {
            const auto pack=state?backpack_reading(c,*state,started):std::nullopt;
            if (!pack) {
                const bool expired=c.now-std::max(started,seen_)>=Time(10000);
                if (!expired && seen_>=started) return {};
                return {expired?GuardState::failed:GuardState::waiting,expired?"Fresh pollen reading unavailable; input released":"Reading pollen counter",expired?std::optional(Interruption::lost_observations):std::nullopt};
            }
            seen_=std::max(seen_,state->backpack_percent.captured);
            if (!count_ || (c.now-sampled_at_>=Time(1000) && seen_>sampled_)) {
                if (seen_-sampled_>Time(2000)) count_=next_=0;
                samples_[next_]=*pack; next_=(next_+1)%samples_.size();
                count_=std::min(count_+1,samples_.size()); sampled_=seen_; sampled_at_=c.now;
            }
            const double average=std::accumulate(samples_.begin(),samples_.begin()+count_,0.)/count_;
            if (average>=*limit_) return {GuardState::completed,"Backpack limit reached"};
            // An unchanged percentage alone is normal with instant conversion
            // or a large backpack. Watch the actual counter digits and rate.
            const auto& activity=state->pollen_changed_at;
            if (gathering_ && activity.has_value(c,Time(2000),started) && activity.value>=Time{}
                && activity.value<=activity.captured && c.now-std::max(started,activity.value)>=std::chrono::minutes(2))
                return {GuardState::failed,"Pollen counter has not changed for 2 minutes; checking field",Interruption::lost_route};
        }
        return {};
    }
};
}
std::unique_ptr<RunGuard> observation_guard(const RunConfig& config,WorkActivity activity) {
    if (config.backpack_limit && (!std::isfinite(*config.backpack_limit) || *config.backpack_limit<1 || *config.backpack_limit>100))
        throw std::invalid_argument("Invalid backpack limit");
    if (!config.observe && !config.backpack_limit) return {};
    return std::make_unique<ObservationGuard>(config,activity);
}
}
