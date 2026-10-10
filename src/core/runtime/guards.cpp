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
public:
    explicit ObservationGuard(const RunConfig& config):limit_(config.backpack_limit) {}
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
        }
        return {};
    }
};
}
std::unique_ptr<RunGuard> observation_guard(const RunConfig& config) {
    if (config.backpack_limit && (!std::isfinite(*config.backpack_limit) || *config.backpack_limit<1 || *config.backpack_limit>100))
        throw std::invalid_argument("Invalid backpack limit");
    if (!config.observe && !config.backpack_limit) return {};
    return std::make_unique<ObservationGuard>(config);
}
}
