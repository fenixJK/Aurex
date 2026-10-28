#include "hop.hpp"
#include "game/detection/daylight.hpp"
#include <atomic>

namespace hive::features::vichop {
void Controller::begin() {
    static std::atomic<std::uint64_t> runs{};
    std::lock_guard lock(mutex_); run_=++runs; hop_=0; state_={}; state_.active=true; state_.epoch=run_; started_=now_;
    target_.clear(); key_.clear(); epoch_.clear();
}
void Controller::stop() { std::lock_guard lock(mutex_); state_.active=false; state_.joined=false; state_.status="Stopped"; target_.clear(); key_.clear(); }
void Controller::release_server() { std::lock_guard lock(mutex_); target_.clear(); key_.clear(); }
void Controller::request(const game::JoinTarget& target) {
    std::lock_guard lock(mutex_);
    target_=target.uri(); epoch_=std::to_string(run_)+"/"+std::to_string(++hop_);
    key_=game::server_request_key(owner_,"vic-server",epoch_,target_);
    state_.joined=false; state_.status="Joining a public server";
}
void Controller::update(const extensions::SessionView& view) {
    std::lock_guard lock(mutex_); now_=view.now; generation_=view.generation;
    if (state_.active) state_.elapsed=std::max(Time{},now_-started_);
    if (state_.active && !state_.joined && !key_.empty() && view.server.joined_request==key_) {
        state_.joined=true; ++state_.servers;
    }
}
void Controller::status(std::string text) { std::lock_guard lock(mutex_); state_.status=std::move(text); }
void Controller::night() { std::lock_guard lock(mutex_); ++state_.nights; }
void Controller::result(Result result,std::string text) {
    std::lock_guard lock(mutex_); state_.last_result=std::move(text);
    switch(result) { case Result::kill:++state_.kills;break;case Result::miss:++state_.misses;break;case Result::skipped:++state_.skipped;break;case Result::failed:++state_.failures;break; }
}
Snapshot Controller::read() const { std::lock_guard lock(mutex_); return state_; }
game::RequestBatch Controller::requests() const {
    std::lock_guard lock(mutex_);
    game::RequestBatch batch{generation_,std::uint64_t(now_.count()),{}};
    if (state_.active && !target_.empty()) batch.channels[extensions::RequestService::servers].push_back(
        {"vic-server",100,now_+Time(3000),{{"target",target_},{"epoch",epoch_},{"interrupt",true}}});
    return batch;
}
namespace {
class ServerVisit final : public automation::Program {
    enum class Phase { select,joining,night,night_view,setup,prepared,hunt,result,complete };
    Services services_;
    std::shared_ptr<Controller> controller_;
    Time duration_,since_{},sample_since_{},sample_{Time(-1)};
    std::optional<bool> night_;
    std::optional<std::string> hop_reason_;
    Phase phase_{Phase::select};
    bool counted_night_{},running_{},needs_setup_{true},kill_counted_{},yield_requested_{},sky_checked_{};
    bool scout_from_spawn_{};
    unsigned sky_steps_{};
    ObservationContext context_;
    void record_kill() {
        if (phase_==Phase::result && !kill_counted_ && services_.enemy.progress->read().defeated) {
            controller_->result(Controller::Result::kill,"Vicious Bee defeated"); kill_counted_=true;
            if(services_.confirmed)services_.confirmed(context_);
        }
    }
    void finish_visit(Controller::Result outcome,std::string reason) {
        hop_reason_=reason;
        if (outcome!=Controller::Result::kill || !kill_counted_) controller_->result(outcome,std::move(reason));
        phase_=Phase::complete;
    }
public:
    ServerVisit(Services services,std::shared_ptr<Controller> controller,Time duration)
        :services_(std::move(services)),controller_(std::move(controller)),duration_(duration) {
        if (!services_.servers || !services_.daylight || !services_.prepare || !services_.home
            || !services_.arrival || !services_.joined_setup
            || !services_.enemy.observations || services_.enemy.hunt_fields.empty() || !controller_
            || duration<=Time{} || duration>std::chrono::hours(8)) throw std::invalid_argument("Vicious hunting needs search fields, travel, daylight and public servers");
        services_.enemy.progress=std::make_shared<combat::FightProgress>();
    }
    ~ServerVisit() override { finished(automation::State::stopped); }
    automation::RunConfig config() const override { return {duration_,services_.speed,{},false,false}; }
    bool server_change_ready() const noexcept override { return phase_==Phase::joining; }
    bool request_yield() override {
        if(phase_!=Phase::select)return false;
        yield_requested_=true;return true;
    }
    automation::ProgramFactory server_arrival() const override { return services_.arrival; }
    void observe(const automation::View& view) override { context_=view.context; record_kill(); }
    bool resume_after_reconnect(const automation::View& view) override {
        context_=view.context;
        record_kill();
        if (kill_counted_) { finish_visit(Controller::Result::kill,"Vicious Bee defeated"); return true; }
        needs_setup_=phase_==Phase::joining;
        scout_from_spawn_=false;
        phase_=Phase::night; sample_since_=since_=view.context.now; sample_=Time(-1); night_.reset(); sky_checked_=false; sky_steps_=0; return true;
    }
    ActivityStatus activity_status() const override {
        return (phase_==Phase::result || (phase_==Phase::prepared && !scout_from_spawn_))
            ?ActivityStatus{}:ActivityStatus{"Vic Hop",controller_->read().status};
    }
    automation::Step next(const automation::View& view) override {
        using namespace automation;
        context_=view.context;
        if (!running_) { if (!controller_->read().active) controller_->begin(); services_.servers->set_active(true); running_=true; }
        if (!controller_->read().active) return Done{"Vic Hop stopped"};
        if(yield_requested_)return Defer{"Vicious hunt yielded for scheduled upkeep"};
        if (hop_reason_) {
            auto reason=std::move(*hop_reason_); hop_reason_.reset();
            return ReportActivity{{"Vic Hop",std::move(reason)}};
        }
        if (phase_==Phase::complete) return Done{controller_->read().last_result};
        if (phase_==Phase::select) {
            const auto choice=services_.servers->next(view.context.now); controller_->status(choice.status);
            if (!choice.target) return Wait{Time(250),choice.status};
            controller_->request(*choice.target); phase_=Phase::joining; since_=view.context.now;
        }
        if (phase_==Phase::joining) {
            if (!controller_->read().joined) {
                if (view.context.now-since_>=std::chrono::minutes(3))
                    throw ObservationUnavailable("Vic Hop could not join the requested public server");
                return Wait{Time(100),"Joining a public server"};
            }
            phase_=Phase::night; sample_since_=since_=view.context.now;
        }
        if (phase_==Phase::night) {
            controller_->status("Checking for night");
            const auto reading=game::read_night(*services_.daylight,view.context,sample_since_);
            if (reading.has_value(view.context,Time(2000),sample_since_) && reading.captured>sample_) {
                if (night_ && *night_==reading.value) {
                    if (!reading.value) { finish_visit(Controller::Result::skipped,"Daytime server skipped"); return Wait{Time(100),"Finding a night server"}; }
                    if (!counted_night_) { controller_->night(); counted_night_=true; }
                    phase_=(needs_setup_ || sky_checked_)?Phase::setup:Phase::hunt;
                }
                night_=reading.value; sample_=reading.captured;
            }
            if (phase_==Phase::night) {
                // A blocked spawn view will not improve by staring at it.
                // Only adjust the camera when the initial view is inconclusive;
                // normal hive setup establishes its own camera after this check.
                if (sky_steps_<11 && (!night_ || view.context.now-sample_>=Time(250))
                    && view.context.now-std::max(since_,sample_since_)>=Time(250)) {
                    // Reveal the sky a few taps at a time, checking fresh frames
                    // between turns instead of always running to the pitch limit.
                    const auto steps=std::min(3u,11-sky_steps_);
                    sky_steps_+=steps; sky_checked_=true; phase_=Phase::night_view;
                    controller_->status("Checking the sky for night");
                    return MoveCamera{{Key::pitch_down,steps}};
                }
                if (view.context.now-since_>=Time(5000)) { finish_visit(Controller::Result::skipped,"Night could not be confirmed"); return Wait{Time(100),"Trying another server"}; }
                return Wait{Time(50),"Checking for night"};
            }
        }
        if (phase_==Phase::setup) {
            scout_from_spawn_=needs_setup_ && services_.spawn_setup && combat::uses_scout_route(services_.enemy);
            controller_->status(scout_from_spawn_?"Night confirmed · Preparing spawn":"Preparing hive on night server"); phase_=Phase::prepared;
            return RunProgram{scout_from_spawn_?services_.spawn_setup:needs_setup_?services_.joined_setup:services_.prepare,true};
        }
        if (phase_==Phase::hunt) {
            controller_->status("Searching for Vicious Bee"); phase_=Phase::result;
            return RunProgram{[services=services_,start=scout_from_spawn_?combat::HuntStart::spawn:combat::HuntStart::hive] {
                return combat::hunt(services.enemy,services.speed,services.prepare,services.home,services.movement,start);
            },true};
        }
        return Wait{Time(100),"Finishing the search"};
    }
    void step_completed(const automation::View& view) override {
        context_=view.context;
        if (phase_==Phase::night_view) {
            phase_=Phase::night; sample_since_=view.context.now+Time(1); sample_=Time(-1); night_.reset();
            return;
        }
        if ((phase_!=Phase::result && phase_!=Phase::prepared) || !view.completed) return;
        record_kill();
        if (view.completed->state==automation::State::stopped || view.completed->state==automation::State::paused) {
            controller_->stop(); return;
        }
        if (phase_==Phase::prepared) {
            if (view.completed->state==automation::State::completed) { needs_setup_=false; phase_=Phase::hunt; }
            else finish_visit(Controller::Result::failed,std::string(scout_from_spawn_?"Spawn setup interrupted: ":"Hive setup interrupted: ")+view.completed->message);
            return;
        }
        const auto result=services_.enemy.progress->read();
        if (result.defeated) finish_visit(Controller::Result::kill,"Vicious Bee defeated");
        else if (view.completed->state!=automation::State::completed)
            finish_visit(Controller::Result::failed,"Search interrupted: "+view.completed->message);
        else finish_visit(Controller::Result::miss,view.completed->message);
    }
    void finished(automation::State state) override {
        if (!running_) return;
        record_kill(); running_=false;
        if(phase_!=Phase::complete && state==automation::State::failed)
            controller_->result(Controller::Result::failed,"Server visit interrupted");
        controller_->release_server();
        services_.servers->set_active(false);
    }
};
}
std::unique_ptr<automation::Program> visit(Services services,std::shared_ptr<Controller> controller,Time duration) {
    return std::make_unique<ServerVisit>(std::move(services),std::move(controller),duration);
}
}
