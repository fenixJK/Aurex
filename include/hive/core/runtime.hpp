#pragma once
#include "model.hpp"
#include <deque>
#include <set>

namespace hive {
enum class RunState { idle, running, paused, completed, stopped, faulted };
enum class CompletionMode { finite, managed };
enum class TaskOutcome { executed, cancelled, expired, faulted };
struct TaskResult {
    std::string id;
    std::uint64_t owner{};
    TaskOutcome outcome{};
    Time finished{};
    std::size_t completed_actions{};
    bool started{};
    std::string reason;
    Time execution_time{};
};
struct Status {
    RunState state{RunState::idle};
    std::string job, action, reason;
    std::size_t completed_actions{}, queued_jobs{};
};
class Runtime {
public:
    explicit Runtime(Input& input): input_(input) {}
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    void start(Window window, Time now, CompletionMode mode=CompletionMode::finite);
    void enqueue(Job job);
    void tick(Time now, const Snapshot& snapshot);
    void pause(Time now);
    void resume(Time now, const Snapshot& snapshot);
    void stop(std::string reason = "Stopped");
    Status status() const;
    void cancel_owner(std::uint64_t owner, Time now);
    bool has_work() const;
    std::vector<TaskResult> take_results(std::uint64_t owner);
private:
    struct Pending { Job job; std::size_t index{}; Time remaining{}; bool begun{}, started{}; Time execution_time{},segment_start{}; };
    Input& input_;
    Window window_;
    Status status_;
    std::deque<Pending> queue_;
    std::optional<Pending> active_;
    std::set<Key> held_;
    std::shared_ptr<PointerTasks> pointer_tasks_;
    Time end_{}, last_tick_{};
    struct HeldTap { Key key; Time until; std::uint64_t owner; };
    std::optional<HeldTap> tap_release_;
    bool interrupted_interaction_{};
    CompletionMode mode_{CompletionMode::finite};
    std::deque<TaskResult> results_;
    void finish(const Pending& pending, TaskOutcome outcome, Time now, std::string reason={});
    bool release(const std::set<Key>& keep={}) noexcept;
    bool stop_pointer() noexcept;
    void fault(std::string reason);
    bool guard(const Snapshot& snapshot);
    void choose(Time now);
};
}
