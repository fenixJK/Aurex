#pragma once
#include "core/runtime/runtime.hpp"
#include "presentation.hpp"
#include "settings.hpp"
#include "core/runtime/program.hpp"
#include "requests.hpp"
#include "work_schedule.hpp"
#include <functional>
#include <map>
#include <memory>
#include <variant>
#include <cmath>

namespace hive::extensions {
inline constexpr std::size_t maximum_page_sections=96;
struct Metric {
    std::string label, value;
    std::optional<double> progress;
    std::string group,color_key;
    bool operator==(const Metric&) const=default;
};
struct HotbarUse {
    unsigned slot{};
    std::string item,label;
    bool operator==(const HotbarUse&) const=default;
};
struct PlotProgress { bool running{}; std::string message; };
struct ModuleReport { std::string id,name; std::vector<Metric> metrics; std::vector<Counter> counters; std::vector<Threshold> thresholds; };
struct Binding { std::string label, signal; };
struct PageSection {
    std::string id,title,description;
    std::vector<std::string> settings;
    bool metrics{};
    unsigned columns{2};
    bool collapsed{};
    std::string collection,choice;
    bool row{};
    std::string tool,tab;
    std::string order_group,order_setting;
    bool inline_labels{};
    std::string panel;
};
struct PageLayout { std::vector<PageSection> sections; bool compact{}; };
namespace RequestService {
inline const std::string fields="fields",enemies="enemies",stations="stations",servers="servers";
}
struct RoutineParticipation {
    int priority{};
    Time recheck{60000};
    bool fallback{},reactive{},demand_only{};
    bool interrupt_gather{true};
};
struct SetupProfile { std::string name; int order{}; Settings settings; };
void validate(const RoutineParticipation& participation);
struct Manifest {
    std::string id, name, version, description, provider;
    int api{1}, settings_version{1};
    std::vector<std::string> dependencies;
    std::vector<Setting> settings;
    std::vector<Binding> bindings;
    std::map<std::string,std::string> scripts;
    std::map<std::string,std::string> sources;
    automation::Permissions permissions;
    std::set<std::string> requests;
    std::optional<RoutineParticipation> routine;
    std::set<std::string> consumes;
    std::optional<PageLayout> page;
    std::vector<Command> commands;
    bool enabled_by_default{};
    bool presets{true};
    std::vector<SetupProfile> setup_profiles;
};
struct SessionView {
    Status status;
    Time now{};
    std::uint64_t generation{};
    std::optional<automation::Progress> feature;
    std::uint64_t pause_requests{};
    game::RequestInbox requests;
    std::uint64_t session_id{};
    game::ServerSession server;
    bool reconnecting{};
};
void validate_settings(const Manifest& manifest,const Settings& settings);
class Module {
public:
    virtual ~Module() = default;
    virtual void start(const Settings&) = 0;
    virtual void update(const SessionView&) = 0;
    virtual std::vector<Metric> metrics() const = 0;
    std::vector<Metric> checked_metrics() const {
        auto result=metrics();
        if (result.size()>128) throw std::runtime_error("Too many display metrics");
        for (const auto& metric:result) if (metric.label.size()>160 || metric.value.size()>4096 || metric.group.size()>160
            || (metric.progress && (!std::isfinite(*metric.progress) || *metric.progress<0 || *metric.progress>1)))
            throw std::runtime_error("Invalid display metric");
        return result;
    }
    virtual std::vector<HotbarUse> hotbar_uses() const { return {}; }
    virtual std::optional<std::vector<Metric>> report_metrics() const { return {}; }
    virtual std::vector<Counter> report_counters() const { return {}; }
    virtual std::vector<Threshold> report_thresholds() const { return {}; }
    virtual void stop() noexcept = 0;
    virtual game::Requests requests() const { return {}; }
    virtual game::RequestBatch request_channels() const { return game::channel_batch(requests()); }
    virtual std::optional<RoutineParticipation> routine() const { return {}; }
    virtual std::unique_ptr<automation::Program> routine_program() const { return program(); }
    virtual std::vector<Command> commands() const { return {}; }
    virtual std::vector<ModuleTimer> timers() const { return {}; }
    virtual void invoke(const std::string&) { throw std::invalid_argument("Unknown module command"); }
    virtual CommandResult execute_command(const std::string& id,const CommandInput&) { invoke(id);return {id,"Completed."}; }
    virtual std::unique_ptr<automation::Program> command_program(const std::string&) const { throw std::invalid_argument("Unknown launch command"); }
    virtual std::unique_ptr<automation::Program> command_program(const std::string& id,const CommandInput&) const { return command_program(id); }
    virtual std::shared_ptr<const Plot> plot() const { return {}; }
    virtual PlotProgress plot_progress() const { return {}; }
    virtual std::map<std::string,std::string> setting_summaries() const { return {}; }
    virtual bool pending_work(const SessionView&) const { return false; }
    virtual std::optional<WorkEstimate> next_work(const SessionView&) const { return {}; }
    virtual std::optional<WorkWindow> protected_work(const SessionView&) const { return {}; }
    virtual bool executable() const noexcept { return false; }
    virtual std::unique_ptr<automation::Program> program() const { return {}; }
};
using Factory = std::function<std::unique_ptr<Module>(const Manifest&)>;
std::string run_state_name(RunState state);
}
