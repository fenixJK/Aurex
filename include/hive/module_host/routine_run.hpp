#pragma once
#include "routine.hpp"
#include "hive/session/session.hpp"

namespace hive::extensions {
enum class RoutineState { ready, running, pausing, paused, stopped, failed };

class RoutineRun {
    Host& host_;
    Session& session_;
    Routine schedule_;
    std::optional<automation::Launch> preparation_;
    bool preparing_{};
    RoutineState state_{RoutineState::ready};
    std::uint64_t session_id_{};
    std::uint64_t pause_requests_{};
    std::string reason_;
    SessionView view();
    void fail(std::string reason);
public:
    RoutineRun(Host& host,Session& session,std::unique_ptr<automation::ActivityPolicy> policy,std::optional<automation::Launch> preparation={},std::vector<std::string> order={});
    ~RoutineRun();
    RoutineRun(const RoutineRun&)=delete;
    RoutineRun& operator=(const RoutineRun&)=delete;
    void start(Window window);
    SessionView tick();
    void request_pause();
    void cancel_pause();
    void resume();
    void stop(std::string reason="Stopped");
    RoutineState state() const { return state_; }
    const std::string& reason() const { return reason_; }
};
}
