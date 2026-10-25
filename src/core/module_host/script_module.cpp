#include "providers.hpp"
#include "core/scripting/program.hpp"
#include "module.hpp"

namespace hive::extensions {
namespace {
class RoutineRun final : public automation::Program {
    std::unique_ptr<automation::Program> program_;
public:
    explicit RoutineRun(std::unique_ptr<automation::Program> program):program_(std::move(program)) {}
    automation::RunConfig config() const override { auto config=program_->config(); config.require_done=true; return config; }
    game::ObservationNeeds observation_needs() const override { return program_->observation_needs(); }
    std::vector<Rect> text_regions() const override { return program_->text_regions(); }
    std::vector<game::FixedLabelQuery> fixed_labels() const override { return program_->fixed_labels(); }
    std::unique_ptr<automation::RunGuard> guard() const override { return program_->guard(); }
    automation::Step next(const automation::View& view) override { return program_->next(view); }
    void observe(const automation::View& view) override { program_->observe(view); }
    void step_completed(const automation::View& view) override { program_->step_completed(view); }
    void finished(automation::State state) override { program_->finished(state); }
};
class ScriptModule final : public Module {
    Manifest manifest_;
    Settings settings_;
    automation::RunConfig config_;
    std::optional<automation::Progress> progress_;
public:
    explicit ScriptModule(Manifest manifest):manifest_(std::move(manifest)) {}
    void start(const Settings& settings) override {
        if (manifest_.routine) validate(*manifest_.routine);
        auto check=scripting::script_program(manifest_.sources,settings);
        config_=check->config();
        settings_=settings;
    }
    void update(const SessionView& view) override {
        progress_=view.feature && view.feature->module_id==manifest_.id?view.feature:std::nullopt;
    }
    std::vector<Metric> metrics() const override {
        std::vector<Metric> values{{"Status",progress_?progress_->message:"Ready"}};
        const auto elapsed=progress_?progress_->elapsed.count()/1000:0;
        values.push_back({"Run time",std::to_string(elapsed)+" / "+std::to_string(config_.duration.count()/1000)+" seconds"});
        if (config_.backpack_limit) {
            const auto pollen=progress_ && progress_->backpack_percent?std::to_string(static_cast<int>(std::round(*progress_->backpack_percent)))+"%":"Waiting for reading";
            values.push_back({"Backpack",pollen+" · stop at "+std::to_string(static_cast<int>(*config_.backpack_limit))+"%"});
        }
        return values;
    }
    std::vector<Command> commands() const override { return manifest_.commands; }
    std::unique_ptr<automation::Program> command_program(const std::string& id,const CommandInput& input) const override {
        for(const auto& command:manifest_.commands)if(command.id==id) {
            auto sources=manifest_.sources;sources["main"]=sources.at(command.script);
            if(command.script!="main")sources.erase(command.script);
            return scripting::script_program(std::move(sources),settings_,input.arguments);
        }
        throw std::invalid_argument("Unknown script command");
    }
    void stop() noexcept override {}
    std::optional<RoutineParticipation> routine() const override { return manifest_.routine; }
    std::unique_ptr<automation::Program> routine_program() const override { return std::make_unique<RoutineRun>(program()); }
    bool executable() const noexcept override { return true; }
    std::unique_ptr<automation::Program> program() const override { return scripting::script_program(manifest_.sources,settings_); }
};
}
Factory script_factory() { return [](const Manifest& manifest) { return std::make_unique<ScriptModule>(manifest); }; }
}
