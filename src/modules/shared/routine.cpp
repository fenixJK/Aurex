#include "routine.hpp"
#include <algorithm>

namespace hive::features::routine {
using namespace automation;
namespace {
class RoutineProgram final : public Program {
    Routine plan_;
    std::vector<Time> due_;
    std::unique_ptr<ActivityPolicy> policy_;
    std::optional<std::size_t> selected_;
    enum class Phase { entry, activity, returning, selecting } phase_{Phase::entry};
    std::size_t next_step_{}, expected_{};
    Time last_{}, confirmed_{}, boost_until_{};
    bool started_{};
    const std::vector<Step>& steps() const { return selected_?plan_.activities[*selected_].steps:plan_.gather; }
    void choose(Time now) {
        selected_.reset();
        std::vector<ActivityOption> options;
        for (std::size_t i=0;i<plan_.activities.size();++i) {
            const auto& candidate=plan_.activities[i];
            if (due_[i]<=now && (boost_until_<=now || candidate.during_boost)) options.push_back({candidate.id,candidate.priority,due_[i]});
        }
        if (const auto choice=policy_->choose(options)) {
            if (std::none_of(options.begin(),options.end(),[&](const ActivityOption& option) { return option.id==*choice; }))
                throw std::invalid_argument("Activity policy selected an ineligible activity");
            for (std::size_t i=0;i<plan_.activities.size();++i) if (plan_.activities[i].id==*choice) { selected_=i; break; }
        }
        next_step_=0; phase_=Phase::activity;
    }
public:
    RoutineProgram(Routine plan,std::unique_ptr<ActivityPolicy> policy):plan_(std::move(plan)),due_(plan_.activities.size()),policy_(std::move(policy)) {
        if (!policy_ || !plan_.config.observe || plan_.config.backpack_limit || plan_.activities.size()>32 || plan_.gather.empty())
            throw std::invalid_argument("Routine needs observations, local gather limits and a gather sequence");
        game::validate(plan_.anchor.target);
        if (plan_.anchor.timeout<Time(100) || plan_.anchor.timeout>Time(60000)) throw std::invalid_argument("Invalid routine anchor timeout");
        std::size_t total=plan_.gather.size(); std::set<std::string> ids;
        auto sequence=[](const std::vector<Step>& steps) {
            if (steps.empty() || std::any_of(steps.begin(),steps.end(),[](const Step& step) { return std::holds_alternative<Done>(step); }))
                throw std::invalid_argument("Activity needs operations without an early done");
        };
        sequence(plan_.gather);
        for (const auto& activity : plan_.activities) {
            total+=activity.steps.size(); sequence(activity.steps);
            if (activity.id.empty() || activity.id.size()>80 || !ids.insert(activity.id).second || activity.priority<0 || activity.priority>100
                || activity.cooldown<Time(1000) || activity.cooldown>std::chrono::hours(24) || activity.boost_duration<Time::zero()
                || activity.boost_duration>std::chrono::hours(4) || activity.confirmation>=activity.steps.size()) throw std::invalid_argument("Invalid routine activity");
            const auto& confirmation=activity.steps[activity.confirmation];
            const auto* pattern=std::get_if<Pattern>(&confirmation);
            if (!std::holds_alternative<UseStation>(confirmation) && !std::holds_alternative<Convert>(confirmation) && !(pattern && pattern->until))
                throw std::invalid_argument("Activity cooldown requires an observed outcome");
        }
        if (total>256) throw std::invalid_argument("Routine exceeds 256 operations");
    }
    RunConfig config() const override { return plan_.config; }
    Step next(const View& view) override {
        if (view.elapsed<last_ || view.completed_steps!=expected_) throw std::logic_error("Routine progress is out of order");
        last_=view.elapsed; ++expected_;
        if (!started_) { started_=true; return plan_.anchor; }
        if (phase_==Phase::entry) phase_=Phase::selecting;
        else if (phase_==Phase::activity) {
            if (selected_ && next_step_==plan_.activities[*selected_].confirmation+1) confirmed_=view.elapsed;
            if (next_step_==steps().size()) { phase_=Phase::returning; return plan_.anchor; }
        } else if (phase_==Phase::returning) {
            if (selected_) {
                const auto& done=plan_.activities[*selected_];
                due_[*selected_]=confirmed_+done.cooldown;
                boost_until_=std::max(boost_until_,confirmed_+done.boost_duration);
            }
            policy_->completed(selected_?std::optional(plan_.activities[*selected_].id):std::nullopt);
            phase_=Phase::selecting;
        }
        if (phase_==Phase::selecting) choose(view.elapsed);
        return steps().at(next_step_++);
    }
};
}
std::unique_ptr<Program> routine_program(Routine routine,std::unique_ptr<ActivityPolicy> policy) {
    return std::make_unique<RoutineProgram>(std::move(routine),std::move(policy));
}
}
