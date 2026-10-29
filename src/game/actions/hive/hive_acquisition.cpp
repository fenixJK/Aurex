#include "hive_setup.hpp"
#include "game/actions/recovery/respawn.hpp"
#include "game/actions/recovery/reconnect.hpp"

namespace hive::automation {
namespace {
enum class Reentry { reset,respawn,reconnect };
class RespawnToHive final : public Program {
    ProgramFactory reset_,acquire_;
    std::shared_ptr<const game::RespawnSource> respawn_;
    std::shared_ptr<HiveSetupResult> result_;
    std::shared_ptr<RespawnResult> reset_result_;
    std::shared_ptr<ReconnectResult> join_result_;
    double speed_;
    unsigned phase_{};
    Reentry reentry_;
public:
    RespawnToHive(ProgramFactory reset,ProgramFactory acquire,std::shared_ptr<const game::RespawnSource> respawn,
        std::shared_ptr<HiveSetupResult> result,double speed,Reentry reentry=Reentry::reset,std::shared_ptr<RespawnResult> reset_result={},std::shared_ptr<ReconnectResult> join_result={})
        :reset_(std::move(reset)),acquire_(std::move(acquire)),respawn_(std::move(respawn)),result_(std::move(result)),reset_result_(std::move(reset_result)),join_result_(std::move(join_result)),speed_(speed),reentry_(reentry) {
        if (!result_) throw std::invalid_argument("Returning by respawn requires confirmed hive setup");
    }
    RunConfig config() const override { return {Time(reentry_==Reentry::reconnect?360000:300000),speed_,{},true,true}; }
    std::string activity_label() const override { return reentry_==Reentry::reconnect?"Rejoining Bee Swarm":"Returning to hive"; }
    bool expects_respawn() const noexcept override { return phase_==0; }
    bool expects_reconnect() const noexcept override { return reentry_==Reentry::reconnect && !phase_; }
    ProgramFactory recovery(Interruption reason,const View&) override {
        if (reason!=Interruption::death) return {};
        result_->invalidate_camera();
        phase_=1;
        return [source=respawn_,result=reset_result_] { return wait_for_respawn(source,result); };
    }
    Step next(const View& view) override {
        if (!phase_) {
            if (reentry_==Reentry::reconnect) result_->reset();
            else result_->begin_preparation(view.context);
            return RunProgram{reset_};
        }
        if (phase_==1) return RunProgram{acquire_};
        const auto result=result_->read(view.context,view.observations);
        if (!result || !result->claimed_slot || !result->camera_prepared
            || !Observation<bool>{Knowledge::present,true,result->context.now,result->context.window,result->context.generation}.has_value(view.context,Time(2000)))
            throw std::runtime_error("Return did not confirm a hive slot and prepared camera");
        return Done{"Returned to a confirmed hive"};
    }
    void step_completed(const View& view) override {
        // A completed Reconnect click reaches the same unclaimed spawn as a
        // relaunched client. Hand that fact to the shared hive setup once.
        if (!phase_ && join_result_ && join_result_->joined.has_value(view.context,Time(2000)) && join_result_->joined.value)
            result_->joined(view.context.window,view.context.now);
        ++phase_;
    }
    void finished(State state) override { if (state!=State::completed) result_->reset(); }
};
class AcquireHive final : public Program {
    enum class Stage { locate,zoom,pitch_limit,pitch,observe,turn,oriented,claim,prepare,recover,done };
    Stage stage_{Stage::locate};
    std::shared_ptr<const game::HiveSlotsSource> slots_;
    std::shared_ptr<const game::LandmarkSource> spawn_,reference_,proximity_;
    HiveClaimOptions options_;
    HiveStart start_;
    HiveGoal goal_;
    Observation<bool> spawn_heading_;
    std::unique_ptr<Program> child_;
    std::unique_ptr<RunGuard> child_guard_;
    Time minimum_{},seen_{Time(-1)},child_started_{},away_since_{Time(-1)};
    std::optional<Time> child_running_;
    std::size_t child_steps_{};
    bool turned_{},issued_{};
    bool started_{},recovered_{};
    std::shared_ptr<RespawnResult> reset_result_;
    bool verify_spawn_{},reset_at_spawn_{};
    unsigned resets_{};
    Observation<bool> spawn_camera_;
    void reserve_reset() {
        if (resets_>=2) throw ClientRejoinRequired("Hive position could not be confirmed after retrying resets; rejoining Bee Swarm");
        ++resets_;
    }
    void observe(Time now) {
        stage_=Stage::observe; minimum_=now+Time(150); seen_=Time(-1);
    }
    void begin(Stage stage,const View& view) {
        stage_=stage;
        const auto camera_start=spawn_camera_.has_value(view.context,Time::max())?HiveCameraStart::spawn_view:HiveCameraStart::unknown;
        spawn_camera_={};
        auto claim=options_; claim.search_opposite_view=false; claim.confirmed_spawn=true;
        if (stage==Stage::recover) reset_result_=std::make_shared<RespawnResult>();
        child_=stage==Stage::recover?request_respawn(options_.recovery,reset_result_):stage==Stage::claim?claim_hive_from_spawn(slots_,std::move(claim),camera_start)
            :prepare_hive_view(reference_,proximity_,options_.result,camera_start);
        child_guard_=child_->guard(); child_started_=view.context.now;
        child_running_.reset(); child_steps_=0; issued_=false;
    }
    Step child_step(const View& view) {
        auto local=view; local.completed_steps=child_steps_;
        // Nested phases share the runner's active clock. Waiting for a menu
        // observation must not consume a second, wall-clock timeout here.
        local.elapsed=child_running_?view.elapsed-*child_running_:Time{};
        issued_=false;
        if (child_guard_) {
            const auto guard=child_guard_->evaluate(local,child_started_);
            if (guard.state==GuardState::waiting) {
                if (!child_running_ && view.context.now-child_started_>=Time(10000)) {
                    if (stage_==Stage::prepare) return recover(view);
                    throw ObservationUnavailable("Hive setup prerequisite was not confirmed");
                }
                return Wait{Time(50),guard.message};
            }
            if (guard.state!=GuardState::ready) throw std::runtime_error(guard.message);
        }
        if (!child_running_) child_running_=view.elapsed;
        if (local.elapsed>=child_->config().duration) {
            if (stage_==Stage::prepare) return options_.recovery?recover(view):Step(Defer{"Hive camera alignment timed out; departure skipped"});
            if (stage_==Stage::recover) throw ClientRejoinRequired("Hive reset timed out; rejoining Bee Swarm");
            throw ObservationUnavailable("Hive setup phase timed out");
        }
        Step step;
        try { step=child_->next(local); }
        catch (const HiveAlignmentError& error) {
            if (stage_!=Stage::prepare) throw;
            // Reset restores a reproducible view before considering a rejoin.
            return options_.recovery?recover(view):Step(Defer{std::string(error.what())+"; departure skipped"});
        }
        catch (const ObservationUnavailable&) {
            if (stage_!=Stage::claim || !recovered_) throw;
            return recover(view);
        }
        if (std::holds_alternative<Done>(step)) {
            child_->finished(State::completed); child_.reset(); child_guard_.reset();
            if (stage_==Stage::recover) {
                verify_spawn_=unconfirmed_reset(view.context);
                recovered_=true; turned_=false; options_.confirmed_spawn=reset_at_spawn_; stage_=Stage::locate; minimum_=view.context.now+Time(1);
                seen_=away_since_=Time(-1); return Wait{};
            }
            if (stage_==Stage::claim) {
                const auto result=options_.result?options_.result->read(view.context,view.observations):std::nullopt;
                if (!result || !result->camera_prepared) { begin(Stage::prepare,view); return child_step(view); }
            }
            stage_=Stage::done; return next(view);
        }
        issued_=true;
        return step;
    }
public:
    AcquireHive(std::shared_ptr<const game::HiveSlotsSource> slots,std::shared_ptr<const game::LandmarkSource> spawn,
        std::shared_ptr<const game::LandmarkSource> reference,std::shared_ptr<const game::LandmarkSource> proximity,HiveClaimOptions options,HiveStart start,
        std::shared_ptr<RespawnResult> reset_result={},HiveGoal goal=HiveGoal::owned_hive)
        :slots_(std::move(slots)),spawn_(std::move(spawn)),reference_(std::move(reference)),proximity_(std::move(proximity)),options_(options),start_(start),
        goal_(goal),reset_result_(std::move(reset_result)),resets_(options.after_respawn?1u:0u) {
        if (!slots_ || !spawn_ || !reference_ || !proximity_) throw std::invalid_argument("Hive setup requires slot, spawn and hive observations");
        if (!std::isfinite(options.speed) || options.speed<8 || options.speed>200 || options.preferred_slot>6 || options.existing_slot>6)
            throw std::invalid_argument("Invalid hive setup speed or preferred slot");
        if (start_==HiveStart::reset && !options_.recovery) throw std::invalid_argument("Hive startup reset needs menu observations");
        if (goal_==HiveGoal::spawn_direction && (!options_.confirmed_spawn || start_!=HiveStart::locate))
            throw std::invalid_argument("Spawn-only preparation requires a confirmed fresh join");
    }
    RunConfig config() const override { return {Time(240000),options_.speed,{},true,true}; }
    std::string activity_label() const override { return stage_==Stage::claim?"Claiming hive":""; }
    std::vector<Rect> text_regions() const override {
        if (child_) {
            auto regions=child_->text_regions();
            if (child_guard_) { const auto requested=child_guard_->text_regions(); regions.insert(regions.end(),requested.begin(),requested.end()); }
            return regions;
        }
        return !options_.confirmed_spawn && (stage_==Stage::locate || stage_==Stage::observe)
            ?std::vector<Rect>{game::hive_proximity_prompt().region}:std::vector<Rect>{};
    }
    game::ObservationNeeds observation_needs() const override {
        auto needs=child_?child_->observation_needs():game::ObservationNeeds{};
        if (child_guard_) { const auto requested=child_guard_->observation_needs(); needs.insert(requested.begin(),requested.end()); }
        return needs;
    }
    std::vector<game::FixedLabelQuery> fixed_labels() const override { return child_?child_->fixed_labels():std::vector<game::FixedLabelQuery>{}; }
    bool expects_respawn() const noexcept override { return child_ && child_->expects_respawn(); }
    bool unconfirmed_reset(const ObservationContext& context) const {
        if (!reset_result_) return false;
        if (!reset_result_->transition.has_value(context,Time::max()))
            throw ObservationUnavailable("Reset result belongs to an unavailable game view");
        return !reset_result_->transition.value;
    }
    ProgramFactory recovery(Interruption reason,const View&) override {
        if (!options_.recovery || (reason!=Interruption::death && reason!=Interruption::lost_observations
            && reason!=Interruption::lost_route)) return {};
        if (reason!=Interruption::death) reserve_reset();
        if (child_) child_->finished(State::stopped);
        child_.reset(); child_guard_.reset(); child_running_.reset();
        reset_result_=std::make_shared<RespawnResult>(); verify_spawn_=false;
        spawn_camera_={};
        if (options_.result) options_.result->invalidate_camera();
        stage_=Stage::locate; started_=turned_=issued_=false; recovered_=true; options_.confirmed_spawn=false;
        seen_=away_since_=Time(-1);
        return [source=options_.recovery,reason,result=reset_result_] {
            return reason==Interruption::death?wait_for_respawn(source,result):request_respawn(source,result);
        };
    }
    Step recover(const View& view) {
        if (!options_.recovery) throw ObservationUnavailable("Hive position could not be found");
        reserve_reset();
        // A stationary direction survey cannot lose a known unclaimed spawn.
        // Once claiming/preparation has moved us, location must be established again.
        reset_at_spawn_=options_.confirmed_spawn && stage_!=Stage::claim && stage_!=Stage::prepare;
        if (options_.result) options_.result->begin_preparation(view.context);
        begin(Stage::recover,view);
        if (resets_>1) return ReportActivity{{"Retrying reset","Hive position was not confirmed after respawn",ActivityKind::diagnostic}};
        return child_step(view);
    }
    Step prepare_owned(const View& view,const Observation<bool>& owned) {
        if (goal_==HiveGoal::spawn_direction)
            throw ObservationUnavailable("Spawn scouting cannot start from an owned hive");
        const auto result=options_.result?options_.result->read():std::nullopt;
        const unsigned slot=result && result->claimed_slot?*result->claimed_slot:options_.existing_slot;
        if (!slot || !options_.result)
            throw std::runtime_error("Select your already claimed hive in Setup, or start at spawn to claim one automatically");
        // Carry the observed ownership into preparation rather than waiting for
        // the same prompt from another source. Preserve its capture timestamp.
        options_.result->claimed(slot,{owned.window,owned.generation,owned.captured});
        begin(Stage::prepare,view); return child_step(view);
    }
    Step locate_unknown(const View& view) {
        // World colors can resemble spawn elsewhere in the map. Only survey
        // from an unlocated client after resetting; a fresh join bypasses this.
        if (!recovered_) return recover(view);
        stage_=Stage::zoom;
        return next(view);
    }
    Step next(const View& view) override {
        if (!started_) {
            verify_spawn_=unconfirmed_reset(view.context);
            recovered_=recovered_ || options_.after_respawn;
            if (options_.result) {
                if (goal_==HiveGoal::spawn_direction) options_.result->reset();
                options_.confirmed_spawn=options_.result->consume_join(view.context) || options_.confirmed_spawn;
                const auto& current=view.context;
                const auto previous=options_.result->read(current,view.observations);
                if (start_==HiveStart::locate && previous && previous->claimed_slot && previous->camera_prepared)
                    return Done{"Hive view is already prepared"};
                options_.result->begin_preparation(current);
            }
            minimum_=view.context.now; started_=true;
            // Session startup needs a reproducible departure position. A hive
            // prompt only proves proximity; a confirmed fresh join already
            // supplies the spawn position and does not need another reset.
            if (start_==HiveStart::reset && !options_.confirmed_spawn && !recovered_) return recover(view);
        }
        const auto& c=view.context;
        switch (stage_) {
        case Stage::claim:
        case Stage::prepare:
        case Stage::recover: return child_step(view);
        case Stage::done: {
            if (goal_==HiveGoal::spawn_direction) return Done{"Spawn direction confirmed"};
            const auto result=options_.result?options_.result->read():std::nullopt;
            if (result && result->claimed_slot) return Done{"Hive "+std::to_string(*result->claimed_slot)+" confirmed and camera prepared"};
            throw std::runtime_error("Select your already claimed hive in Setup, or start at spawn to claim one automatically");
        }
        case Stage::zoom: return MoveCamera{{Key::zoom_in,10}};
        case Stage::pitch_limit: return MoveCamera{game::pitch_limit(Key::pitch_down)};
        case Stage::pitch: return MoveCamera{{Key::pitch_up,hive_spawn_pitch_steps}};
        case Stage::turn: return MoveCamera{{Key::rotate_right,4}};
        case Stage::oriented: return AdoptHeading{0,spawn_heading_};
        case Stage::locate: {
            // Missing the death frame does not imply an unclaimed spawn. A
            // fresh owned-hive prompt can enter camera alignment; its visual
            // reference must still succeed before any departure is allowed.
            if (verify_spawn_ && !options_.confirmed_spawn) {
                if (view.observations) {
                    const auto& text=view.observations->text;
                    const auto owned=game::read_prompt_landmark(c,text,game::hive_proximity_prompt());
                    const auto claim=game::read_prompt_landmark(c,text,game::hive_claim_prompt());
                    if (owned.has_value(c,Time(2000)) && owned.value && !claim.value)
                        return prepare_owned(view,owned);
                }
                stage_=Stage::zoom; return next(view);
            }
            if (options_.confirmed_spawn) {
                if (view.observations && view.observations->game_hud.has_value(c,Time(2000)) && view.observations->game_hud.value) {
                    stage_=Stage::zoom; return next(view);
                }
                if (c.now-minimum_>=game_loading_timeout) throw ClientRejoinRequired("Bee Swarm did not finish loading");
                return Wait{Time(100),"Waiting for Bee Swarm to load"};
            }
            const auto wait_for_prompt=[&]() -> Step {
                if (c.now-minimum_>=Time(10000)) return locate_unknown(view);
                return Wait{Time(50),recovered_?"Waiting for the hive prompt after respawn":"Reading hive interaction prompt"};
            };
            if (!view.observations) return wait_for_prompt();
            const auto& text=view.observations->text;
            // Neither a pre-reset capture nor the HUD behind the dying
            // character establishes where the player has respawned.
            if (!text.has_value(c,Time(2000),recovered_?minimum_:Time{}))
                return wait_for_prompt();
            if (text.captured==seen_) return wait_for_prompt();
            seen_=text.captured;
            const auto owned=game::read_prompt_landmark(c,text,game::hive_proximity_prompt());
            const auto claim=game::read_prompt_landmark(c,text,game::hive_claim_prompt());
            if (owned.value && claim.value) {
                return wait_for_prompt();
            }
            const int candidate=owned.value?1:claim.value?2:0;
            if (candidate==2 && options_.result) options_.result->reset();
            if (candidate==1) {
                return prepare_owned(view,owned);
            }
            if (recovered_ && !candidate) {
                // Once death/revival was actually observed, fresh live frames
                // without a hive prompt can proceed to the spawn survey. Do
                // not spend the entire slow-respawn allowance at an empty pad.
                const auto& state=*view.observations;
                const bool revived=reset_result_ && reset_result_->transition.has_value(c,Time::max())
                    && reset_result_->transition.value && state.alive.has_value(c,Time(2000),minimum_) && state.alive.value
                    && state.game_hud.has_value(c,Time(2000),minimum_) && state.game_hud.value;
                if (!revived) away_since_=Time(-1);
                else if (away_since_<Time{}) away_since_=text.captured;
                else if (text.captured-away_since_>=Time(300)) return locate_unknown(view);
                return wait_for_prompt();
            }
            return locate_unknown(view);
        }
        case Stage::observe: break;
        }
        if (view.observations && !options_.confirmed_spawn) {
            const auto& text=view.observations->text;
            const auto owned=game::read_prompt_landmark(c,text,game::hive_proximity_prompt());
            const auto claim=game::read_prompt_landmark(c,text,game::hive_claim_prompt());
            // Camera turns invalidate the view reference, not a fresh proximity prompt.
            if (owned.has_value(c,Time(2000)) && owned.value && !claim.value) {
                return prepare_owned(view,owned);
            }
        }
        if (c.now<minimum_) return Wait{Time(50),"Settling spawn camera"};
        if (c.now-minimum_>=Time(5000)) return recover(view);
        const auto source=options_.confirmed_spawn && options_.spawn_direction?options_.spawn_direction:spawn_;
        auto reading=source->read(c);
        if (source!=spawn_ && !reading.usable(c,Time(2000),minimum_)) {
            // Direction is a fast path, not a veto of stronger spawn evidence.
            // A missing strict reference says nothing about yaw, so only use a
            // fresh positive fallback; never turn on its negative result.
            const auto reference=spawn_->read(c);
            if (reference.has_value(c,Time(2000),minimum_) && reference.value) reading=reference;
        }
        if (reading.usable(c,Time(2000),minimum_) && reading.captured>seen_) {
            seen_=reading.captured;
            if (reading.knowledge==Knowledge::present && reading.value) {
                if (goal_==HiveGoal::spawn_direction) {
                    spawn_heading_=reading;stage_=Stage::oriented;return next(view);
                }
                begin(Stage::claim,view); return child_step(view);
            }
            if (!turned_) { stage_=Stage::turn; return next(view); }
            // A single negative frame after the turn may still show the
            // moving camera or loading hive row. Use the existing bounded
            // observation window before spending another reset attempt.
        }
        return Wait{Time(50),options_.confirmed_spawn?"Checking spawn direction":"Checking the settled spawn view"};
    }
    void step_completed(const View& view) override {
        switch (stage_) {
        case Stage::zoom: stage_=Stage::pitch_limit; break;
        case Stage::pitch_limit: stage_=Stage::pitch; break;
        case Stage::pitch:
            spawn_camera_={Knowledge::present,true,view.context.now,view.context.window,view.context.generation};
            // A fresh join confirms the platform position, not the camera yaw.
            // Use the same observed heading check as a post-reset spawn.
            observe(view.context.now);
            break;
        case Stage::turn: turned_=true; observe(view.context.now); break;
        case Stage::oriented: stage_=Stage::done; break;
        case Stage::claim:
        case Stage::prepare:
        case Stage::recover:
            if (issued_) {
                auto local=view; local.completed_steps=++child_steps_; local.elapsed=view.elapsed-*child_running_;
                child_->step_completed(local); issued_=false;
            }
            break;
        default: break;
        }
    }
    void finished(State state) override {
        if (child_) child_->finished(state);
        if (options_.result && state!=State::completed) options_.result->reset();
    }
};
}
std::unique_ptr<Program> acquire_hive(std::shared_ptr<const game::HiveSlotsSource> slots,
    std::shared_ptr<const game::LandmarkSource> spawn,std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,HiveClaimOptions options,HiveStart start,HiveGoal goal) {
    return std::make_unique<AcquireHive>(std::move(slots),std::move(spawn),std::move(reference),std::move(proximity),options,start,nullptr,goal);
}
std::unique_ptr<Program> respawn_to_hive(std::shared_ptr<const game::HiveSlotsSource> slots,
    std::shared_ptr<const game::LandmarkSource> spawn,std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,HiveClaimOptions options) {
    if (!options.recovery) throw std::invalid_argument("Returning by respawn needs menu observations");
    auto recovery=options.recovery; options.confirmed_spawn=false; options.after_respawn=true;
    auto reset_result=std::make_shared<RespawnResult>();
    acquire_hive(slots,spawn,reference,proximity,options);
    return std::make_unique<RespawnToHive>([recovery,reset_result] { return request_respawn(recovery,reset_result); },
        [slots,spawn,reference,proximity,options,reset_result] {
            return std::make_unique<AcquireHive>(slots,spawn,reference,proximity,options,HiveStart::locate,reset_result);
        },recovery,options.result,options.speed,Reentry::reset,reset_result);
}
std::unique_ptr<Program> recover_to_hive(std::shared_ptr<const game::HiveSlotsSource> slots,
    std::shared_ptr<const game::LandmarkSource> spawn,std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,HiveClaimOptions options) {
    if (!options.recovery) throw std::invalid_argument("Death recovery needs game observations");
    options.confirmed_spawn=false; options.after_respawn=true;
    auto reset_result=std::make_shared<RespawnResult>();
    acquire_hive(slots,spawn,reference,proximity,options);
    return std::make_unique<RespawnToHive>([source=options.recovery,reset_result] { return wait_for_respawn(source,reset_result); },
        [slots,spawn,reference,proximity,options,reset_result] {
            return std::make_unique<AcquireHive>(slots,spawn,reference,proximity,options,HiveStart::locate,reset_result);
        },options.recovery,options.result,options.speed,Reentry::respawn,reset_result);
}

std::unique_ptr<Program> reconnect_to_hive(std::shared_ptr<const game::HiveSlotsSource> slots,
    std::shared_ptr<const game::LandmarkSource> spawn,std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,HiveClaimOptions options,ReconnectStart start) {
    if (!options.recovery) throw std::invalid_argument("Reconnect needs game-view observations");
    if (start!=ReconnectStart::request && start!=ReconnectStart::loading) throw std::invalid_argument("Invalid reconnect starting phase");
    options.confirmed_spawn=start==ReconnectStart::loading;
    auto join_result=std::make_shared<ReconnectResult>();
    acquire_hive(slots,spawn,reference,proximity,options);
    return std::make_unique<RespawnToHive>([source=options.recovery,start,join_result] { return start==ReconnectStart::loading?wait_for_reconnect(source):request_reconnect(source,join_result); },
        [slots,spawn,reference,proximity,options] { return acquire_hive(slots,spawn,reference,proximity,options); },options.recovery,options.result,options.speed,Reentry::reconnect,nullptr,join_result);
}

}
