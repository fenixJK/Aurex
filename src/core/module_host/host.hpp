#pragma once
#include "module.hpp"
#include <QString>
#include <QStringList>
#include <QJsonObject>

namespace hive::extensions {
Manifest read_manifest(const QString& file);
Settings defaults(const Manifest& manifest);
void validate_page(const Manifest& manifest);
struct Entry {
    Manifest manifest;
    Settings settings;
    bool requested{};
    bool config_valid{true};
    std::string issue;
    std::vector<Metric> display;
    ModuleReport report() const {
        ModuleReport result{manifest.id,manifest.name,{},{},{}};
        if (instance_) {
            auto metrics=instance_->report_metrics(); result.metrics=metrics?std::move(*metrics):instance_->checked_metrics();
            result.counters=instance_->report_counters(); result.thresholds=instance_->report_thresholds();
        }
        if (!issue.empty()) result.metrics.insert(result.metrics.begin(),{"Issue",issue});
        return result;
    }
    Entry(Manifest metadata,Settings values):manifest(std::move(metadata)),settings(std::move(values)) {}
    bool active() const { return bool(instance_); }
    bool executable() const { return instance_ && instance_->executable(); }
    std::optional<RoutineParticipation> participation() const { return instance_?instance_->routine():std::nullopt; }
    std::vector<ModuleTimer> timers() const { return instance_?instance_->timers():std::vector<ModuleTimer>{}; }
    std::vector<Command> commands() const { return instance_?instance_->commands():std::vector<Command>{}; }
    std::shared_ptr<const Plot> plot() const { return instance_?instance_->plot():nullptr; }
    PlotProgress plot_progress() const { return instance_?instance_->plot_progress():PlotProgress{}; }
    std::map<std::string,std::string> setting_summaries() const { return instance_?instance_->setting_summaries():std::map<std::string,std::string>{}; }
    bool pending_work(const SessionView& view) const { return instance_ && instance_->pending_work(view); }
    std::uint64_t revision() const { return revision_; }
private:
    friend class Host;
    std::unique_ptr<Module> instance_;
    std::uint64_t revision_{};
    std::optional<game::RequestScope> request_scope_;
    void stop() noexcept;
};
struct ModuleConfiguration { Settings settings; bool enabled{}; };
using Configuration=std::map<std::string,ModuleConfiguration>;
struct RoutineEntry {
    std::string id;
    std::uint64_t revision{};
    RoutineParticipation participation;
};
class Host {
public:
    using FailureHandler = std::function<void(const std::string&,const std::string&)>;
    using SettingsMigration = std::function<Settings(int,const Settings&)>;
    explicit Host(QString state_file,std::function<bool()> session_active={},FailureHandler failure_handler={});
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    const CommandRegistry& commands() const { return commands_; }
    CommandResult invoke_command(const std::string& owner,const std::string& command,CommandInput input={},bool remote=false);
    void set_command_launcher(std::function<void(automation::Launch)> launcher) { command_launcher_=std::move(launcher); }
    void register_provider(std::string name, Factory factory,game::RequestGrants requests={},bool installable=false);
    void register_request_contract(std::string id,std::function<void(const Settings&)> validator);
    void register_module(Manifest manifest,std::string unavailable={},SettingsMigration migration={});
    void replace_providers(const std::function<void(Host&)>& registration);
    std::string install(const QString& source,const QString& destination);
    void discover(const QStringList& roots);
    void enable(const std::string& id);
    void disable(const std::string& id);
    void configure(const std::string& id, Settings settings);
    QJsonObject preset() const;
    void apply_preset(const QJsonObject& preset);
    void update(const SessionView& view);
    void fail_module(const std::string& id,const std::string& reason);
    bool pending_work(const std::string& id,const SessionView& view);
    automation::Launch prepare(const std::string& id) const;
    automation::Launch prepare_command(const std::string& id,const std::string& command,const CommandInput& input={}) const;
    std::vector<RoutineEntry> routine_entries() const;
    automation::Launch prepare_routine(const RoutineEntry& entry) const;
    std::vector<Metric> metrics(const std::string& id) const;
    std::vector<game::FieldGoal> field_goals(Time now,std::uint64_t generation) const;
    std::shared_ptr<const game::FieldGoalSource> field_goal_source() const { return requests_; }
    std::shared_ptr<const game::EnemyGoalSource> enemy_goal_source() const { return requests_; }
    std::shared_ptr<const game::RequestSource> request_source() const { return requests_; }
    const std::map<std::string, Entry>& entries() const { return entries_; }
    const std::vector<std::string>& diagnostics() const { return diagnostics_; }
    std::uint64_t configuration_revision() const { return configuration_revision_; }
    void set_display_interest(std::optional<std::string> id) { display_interest_=std::move(id); selective_display_=true; }
    void set_shared_hotbar(std::vector<HotbarUse> uses);
    const std::map<std::string,std::vector<std::string>>& hotbar_warnings() const;
private:
    std::uint64_t configuration_revision_{1},order_revision_{};
    std::vector<std::string> update_order_;
    bool selective_display_{};
    std::optional<std::string> display_interest_;
    std::vector<HotbarUse> shared_hotbar_;
    mutable std::uint64_t hotbar_revision_{};
    mutable std::map<std::string,std::vector<std::string>> hotbar_warnings_;
    CommandRegistry commands_;
    std::function<void(automation::Launch)> command_launcher_;
    CommandRegistry::Handler command_handler(const std::string& owner);
    QString state_file_;
    std::map<std::string, Factory> factories_;
    std::set<std::string> installable_;
    std::map<std::string,game::RequestGrants> request_access_;
    game::RequestContracts request_contracts_{game::game_request_contracts()};
    std::shared_ptr<game::SharedRequests> requests_{std::make_shared<game::SharedRequests>()};
    struct Builtin { Manifest manifest; std::string unavailable; SettingsMigration migration; };
    std::map<std::string,Builtin> builtins_;
    std::map<std::string, Entry> entries_;
    std::vector<std::string> diagnostics_;
    QJsonObject saved_;
    bool state_writable_{true};
    std::function<bool()> session_active_;
    FailureHandler failure_handler_;
    std::uint64_t next_revision_{1};
    void require_stopped() const;
    std::vector<std::string> order(const std::string& id) const;
    void persist(const Configuration* configuration=nullptr);
    void restore();
    void stop_all() noexcept;
    void stop_entry(Entry& entry) noexcept;
    void stop_dependents(const std::string& id, const std::string& reason);
    void activate(const std::string& id);
};
}
