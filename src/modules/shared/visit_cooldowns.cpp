#include "visit_cooldowns.hpp"
#include <algorithm>
#include <tuple>

namespace hive::features::visits {
namespace {
class SavedAvailability final : public automation::AvailabilitySource {
    std::shared_ptr<const automation::AvailabilitySource> source_;
    std::shared_ptr<Cooldowns> saved_;
    std::map<std::string,Time> durations_;
public:
    SavedAvailability(std::shared_ptr<const automation::AvailabilitySource> source,std::shared_ptr<Cooldowns> saved,const automation::VisitPlan& plan)
        :source_(std::move(source)),saved_(std::move(saved)) {
        for (const auto& visit : plan.visits) durations_.emplace(visit.id,visit.cooldown);
    }
    std::vector<Rect> text_regions(const std::string& id) const override { return source_->text_regions(id); }
    Observation<bool> read(const std::string& id,const automation::View& view) const override {
        auto live=source_->read(id,view);
        if (live.has_value(view.context,Time(2000)) && live.value) return live;
        const auto found=durations_.find(id);
        if (found!=durations_.end() && saved_->remaining(id,found->second)>Time{})
            return {Knowledge::present,false,view.context.now,view.context.window,view.context.generation};
        return live;
    }
    Observation<Time> remaining(const std::string& id,const automation::View& view) const override { return source_->remaining(id,view); }
};
class SavedProgram final : public automation::Program {
    automation::VisitRun run_;
    std::shared_ptr<Cooldowns> saved_;
    std::map<std::string,std::tuple<std::optional<Time>,std::optional<Time>,std::optional<Time>>> written_;
    std::optional<ObservationContext> context_;
    void start(const automation::View& view) { saved_->load(); saved_->flush(); context_=view.context; }
    void flush() {
        if (!context_) return;
        const auto& c=*context_; const auto report=run_.results->read();
        if (!report.generation || report.generation!=c.generation || report.window.id!=c.window.id
            || report.window.process!=c.window.process || report.window.bounds!=c.window.bounds) return;
        for (const auto& visit : report.visits) {
            if (!visit.observed_ready_at && (!visit.confirmed_at || !visit.estimated_ready_at)) continue;
            const auto signature=std::tuple{visit.confirmed_at,visit.estimated_ready_at,visit.observed_ready_at};
            if (const auto old=written_.find(visit.id); old!=written_.end() && old->second==signature) continue;
            written_[visit.id]=signature;
            if (visit.observed_ready_at) {
                if (*visit.observed_ready_at>=c.now) saved_->remember(visit.id,c.now,*visit.observed_ready_at,false,c);
            } else saved_->remember(visit.id,*visit.confirmed_at,*visit.estimated_ready_at,true,c);
        }
        saved_->flush();
    }
public:
    SavedProgram(automation::VisitRun run,std::shared_ptr<Cooldowns> saved):run_(std::move(run)),saved_(std::move(saved)) {}
    automation::RunConfig config() const override { return run_.program->config(); }
    ActivityStatus activity_status() const override { return run_.program->activity_status(); }
    WorkActivity activity() const noexcept override { return run_.program->activity(); }
    automation::ProgramFactory recovery(automation::Interruption reason,const automation::View& view) override {
        auto result=run_.program->recovery(reason,view); flush(); return result;
    }
    std::vector<Rect> text_regions() const override { return run_.program->text_regions(); }
    std::vector<game::FixedLabelQuery> fixed_labels() const override { return run_.program->fixed_labels(); }
    std::unique_ptr<automation::RunGuard> guard() const override { return run_.program->guard(); }
    bool request_yield() override { return run_.program->request_yield(); }
    bool resume_after_reconnect(const automation::View& view) override { return run_.program->resume_after_reconnect(view); }
    bool expects_respawn() const noexcept override { return run_.program->expects_respawn(); }
    void observe(const automation::View& view) override { start(view); run_.program->observe(view); flush(); }
    automation::Step next(const automation::View& view) override { start(view); auto step=run_.program->next(view); flush(); return step; }
    void step_completed(const automation::View& view) override { start(view); run_.program->step_completed(view); flush(); }
    void finished(automation::State state) override { run_.program->finished(state); flush(); }
};
}
std::shared_ptr<const automation::AvailabilitySource> saved_availability(std::shared_ptr<const automation::AvailabilitySource> source,
    std::shared_ptr<Cooldowns> saved,const automation::VisitPlan& plan) {
    if (!source || !saved) throw std::invalid_argument("Saved availability needs observations and cooldown state");
    return std::make_shared<SavedAvailability>(std::move(source),std::move(saved),plan);
}
std::unique_ptr<automation::Program> save_cooldowns(automation::VisitRun run,std::shared_ptr<Cooldowns> saved) {
    if (!run.program || !run.results || !saved) throw std::invalid_argument("Saved cooldowns need a visit program and outcomes");
    return std::make_unique<SavedProgram>(std::move(run),std::move(saved));
}
}
