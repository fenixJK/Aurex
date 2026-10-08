#pragma once
#include "hive/platform/platform.hpp"
#include "hive/session/client_rejoin.hpp"
#include "hive/core/runtime.hpp"
#include "hive/core/tasks.hpp"
#include "hive/automation/runner.hpp"
#include "hive/session/observer.hpp"
#include "hive/automation/hotbar.hpp"
#include "hive/module_host/requests.hpp"
#include <mutex>
#include <thread>
#include <atomic>

namespace hive {
struct ActivityEvent { ActivityStatus status; std::shared_ptr<const Frame> frame; std::optional<Rect> image_region; };
struct SessionSnapshot {
    Status status;
    Time now{};
    std::uint64_t generation{};
    std::optional<automation::Progress> feature;
    std::uint64_t pause_requests{};
    std::uint64_t session_id{};
    Window window;
    RecoveryCounts recovery;
};
class Session : public TaskPort {
public:
    explicit Session(Desktop& desktop);
    ~Session();
    void start(Window window, std::vector<Job> jobs);
    void start_managed(Window window);
    void configure_navigation(automation::NavigationServices navigation);
    void configure_rejoin(std::optional<game::JoinTarget> target);
    void configure_server_requests(std::shared_ptr<const game::RequestSource> requests);
    ClientRejoin connect_client(Time now,Window replace={});
    void configure_connection_recovery(automation::ProgramFactory reconnect,automation::ProgramFactory replacement);
    void configure_conversion(std::shared_ptr<game::HiveConversion> conversion);
    void configure_hotbar(std::shared_ptr<automation::HotbarSource> source);
    void configure_speed(std::shared_ptr<const game::MovementSpeedSource> source);
    std::map<std::string,std::shared_ptr<const game::DetectionSource>> configure_detectors(std::vector<game::DetectorConfig> detectors);
    void start_feature(Window window,automation::Launch launch);
    void run_feature(automation::Launch launch);
    bool yield_feature(const std::string& module_id);
    void stop_feature(const std::string& module_id,const std::string& reason);
    std::optional<automation::Progress> feature_progress();
    game::ObservedState observations();
    std::shared_ptr<const Frame> latest_frame() const;
    std::vector<ActivityEvent> take_activity_events();
    ModuleScope grant(ModuleGrant grant);
    void revoke(ModuleScope scope);
    bool acquire(ModuleScope scope) override;
    void release(ModuleScope scope) override;
    std::string submit(ModuleScope scope,TaskClass kind,std::vector<Action> actions,Time lifetime) override;
    std::vector<TaskResult> results(ModuleScope scope) override;
    void cancel(ModuleScope scope) override;
    void pause();
    void pause_between_features();
    void resume();
    void stop(std::string reason="Stopped");
    Status status();
    SessionSnapshot snapshot();
    std::uint64_t generation();
private:
    Desktop& desktop_;
    game::Observer observer_;
    std::mutex mutex_;
    Runtime runtime_;
    TaskBroker tasks_{runtime_};
    std::unique_ptr<TaskPort> feature_tasks_;
    std::unique_ptr<automation::Runner> feature_;
    std::shared_ptr<game::HiveConversion> hive_conversion_;
    std::shared_ptr<automation::HotbarSource> hotbar_;
    std::optional<ModuleScope> feature_scope_;
    std::string feature_id_;
    ModuleGrant feature_grant_;
    automation::NavigationServices navigation_;
    automation::ProgramFactory connection_reconnect_,connection_replacement_;
    bool managed_{};
    bool continue_features_{};
    std::uint64_t window_id_{};
    Window active_window_;
    std::uint64_t session_id_{};
    std::shared_ptr<RecoveryCounts> recovery_counts_{std::make_shared<RecoveryCounts>()};
    using WindowIdentities=std::set<std::pair<std::uint64_t,std::uint64_t>>;
    std::optional<WindowIdentities> reconnect_windows_;
    struct WindowRecovery { Time deadline{},next_scan{}; };
    std::optional<WindowRecovery> window_recovery_;
    std::optional<game::JoinTarget> rejoin_target_;
    std::optional<ClientRejoin> client_rejoin_;
    std::shared_ptr<const game::RequestSource> server_requests_;
    std::optional<game::ServerGoal> requested_server_;
    std::string joined_server_;
    Time next_server_read_{};
    bool read_server_request(Time now);
    std::optional<game::JoinTarget> rejoin_target(Time now) const;
    bool requested_rejoin(Time now);
    bool pause_down_{};
    Time startup_window_missing_{Time(-1)};
    std::uint64_t pause_requests_{};
    std::vector<ActivityEvent> activity_events_;
    std::atomic<bool> exiting_{false};
    std::thread worker_;
    void clear_feature(bool stop_observer=true);
    void launch_feature(Window window,automation::Launch launch,Time now);
    bool begin_window_recovery(Time now);
    void recover_window(Time now);
    bool begin_client_rejoin(Time now,bool requested=false);
    void adopt_window(const Window& window,Time now);
};
}
