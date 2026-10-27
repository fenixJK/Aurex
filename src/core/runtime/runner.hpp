#pragma once
#include "program.hpp"
#include "game/data/hive_preferences.hpp"
#include "game/actions/travel/services.hpp"
#include "tasks.hpp"
#include "pointer_tasks.hpp"
#include "game/detection/state.hpp"
#include "game/actions/interaction/interaction.hpp"
#include "game/actions/hive/conversion.hpp"

namespace hive::automation {
class Runner : private TaskPort {
public:
    Runner(std::unique_ptr<Program> program,Permissions permissions,TaskPort& tasks,ModuleScope scope,NavigationServices navigation={},unsigned depth=0);
    ~Runner();
    using ActivitySink=std::function<void(const ReportActivity&)>;
    void configure_reporting(ActivitySink sink) { activity_sink_=std::move(sink); }
    void configure_conversion(std::shared_ptr<game::HiveConversion> conversion) { hive_conversion_=std::move(conversion); }
    void start(const ObservationContext& context,bool joining=false);
    void configure_connection_recovery(ProgramFactory reconnect,ProgramFactory replacement);
    void observe_movement(const ObservationContext& context,const game::State& observations);
    void update(const ObservationContext& context,const game::State& observations={});
    void pause(Time now,std::string reason="Paused");
    void resume(const ObservationContext& context);
    void stop(std::string reason="Stopped");
    void fail(std::string reason);
    Progress progress() const;
    void configure_statistics(std::shared_ptr<RecoveryCounts> counts) { recovery_counts_=std::move(counts); }
    bool expects_respawn() const noexcept;
    bool expects_reconnect() const noexcept;
    bool waiting_for_game_view() const noexcept;
    void rebase_startup_window(const ObservationContext& context);
    bool can_replace_window() const noexcept;
    void allow_client_rejoin(bool allowed) { allow_rejoin_=allowed; }
    bool needs_client_rejoin() const { return progress_.state==State::running && awaiting_window_ && rejoin_requested_; }
    bool server_change_ready() const noexcept;
    bool prepare_client_rejoin(Time now,bool requested=false);
    bool suspend_window(Time now);
    bool wait_window(Time now,const std::string& status={});
    void replace_window(const ObservationContext& context,ModuleScope scope);
    bool request_yield();
    game::ObservationNeeds observation_needs() const;
    bool needs_movement_speed() const;
    std::vector<Rect> text_regions() const;
    std::vector<game::FixedLabelQuery> fixed_labels() const;
private:
    std::unique_ptr<Program> program_;
    std::shared_ptr<RecoveryCounts> recovery_counts_;
    Permissions permissions_;
    TaskPort& tasks_;
    ModuleScope scope_;
    RunConfig config_;
    std::shared_ptr<game::MovementSpeed> speed_;
    std::unique_ptr<RunGuard> guard_;
    ObservationContext context_;
    Progress progress_;
    ActivitySink activity_sink_;
    std::optional<Step> step_;
    Time last_{}, hold_left_{};
    Time prerequisites_started_{};
    Time death_seen_{Time(-1)};
    bool clock_started_{};
    std::unique_ptr<Runner> pattern_;
    std::unique_ptr<Runner> child_;
    std::unique_ptr<Runner> recovery_;
    ProgramFactory replacement_;
    ProgramFactory connection_reconnect_,connection_replacement_;
    bool startup_pending_{},startup_joining_{},connection_recovering_{},resume_connected_{};
    unsigned connection_attempts_{};
    bool awaiting_window_{},window_replaced_{},allow_rejoin_{},rejoin_requested_{};
    unsigned depth_{};
    std::optional<workflows::Interaction> interaction_;
    std::optional<workflows::Conversion> conversion_;
    std::shared_ptr<game::HiveConversion> hive_conversion_;
    std::optional<workflows::Checkpoint> checkpoint_;
    std::optional<workflows::Sprinkler> sprinkler_;
    std::optional<workflows::InventoryDrag> inventory_;
    std::optional<workflows::InventoryDrag> travel_inventory_;
    std::shared_ptr<PointerTasks> travel_pointer_;
    std::string navigation_input_;
    std::optional<workflows::Dialog> dialog_;
    NavigationServices navigation_services_;
    std::optional<workflows::Navigation> navigation_;
    Time step_started_{},confirmation_frame_{Time(-1)};
    bool baseline_clear_{};
    std::string pending_,menu_pending_;
    Time menu_after_{};
    std::optional<Time> menu_wait_started_;
    unsigned menu_attempts_{};
    std::optional<game::MenuTab> closing_menu_;
    std::function<std::optional<game::MenuTab>()> parent_menu_;
    std::optional<game::MenuTab> movement_menu() const;
    bool owned_{};
    std::optional<unsigned> pending_heading_;
    void finish(State state,std::string message,std::optional<Interruption> interruption={});
    void interrupt(Interruption interruption,const ObservationContext& context,const game::State& observations,std::string message);
    void checkpoint_failed(const ObservationContext& context,const game::State& observations);
    void clear_operations();
    bool recover(Interruption interruption,const ObservationContext& context,const game::State& observations);
    bool same_context(const ObservationContext& context) const;
    bool prepare_menu(const ObservationContext& context,const game::State& observations);
    bool prepares_menu() const;
    bool activity(const ObservationContext& context,const game::State& observations);
    void complete_step(const ObservationContext& context,const game::State& observations);
    void check_scope(ModuleScope scope) const;
    bool acquire(ModuleScope scope) override;
    void release(ModuleScope scope) override;
    std::string submit(ModuleScope scope,TaskClass kind,std::vector<Action> actions,Time lifetime) override;
    std::vector<TaskResult> results(ModuleScope scope) override;
    void cancel(ModuleScope scope) override;
};
}
