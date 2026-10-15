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
    double speed_;
    unsigned phase_{};
    Reentry reentry_;
public:
    RespawnToHive(ProgramFactory reset,ProgramFactory acquire,std::shared_ptr<const game::RespawnSource> respawn,
        std::shared_ptr<HiveSetupResult> result,double speed,Reentry reentry=Reentry::reset)
        :reset_(std::move(reset)),acquire_(std::move(acquire)),respawn_(std::move(respawn)),result_(std::move(result)),speed_(speed),reentry_(reentry) {
        if (!result_) throw std::invalid_argument("Returning by respawn requires confirmed hive setup");
    }
    RunConfig config() const override { return {Time(reentry_==Reentry::reconnect?360000:300000),speed_,{},true,true}; }
    std::string activity_label() const override { return reentry_==Reentry::reconnect?"Rejoining Bee Swarm":"Returning to hive"; }
    bool expects_respawn() const noexcept override { return phase_<2; }
    bool expects_reconnect() const noexcept override { return reentry_==Reentry::reconnect && !phase_; }
    ProgramFactory recovery(Interruption reason,const View&) override {
        if (reason!=Interruption::death) return {};
        phase_=1;
        return [source=respawn_] { return wait_for_respawn(source); };
    }
    Step next(const View& view) override {
        if (!phase_) {
            if (reentry_==Reentry::reconnect) result_->reset();
            else result_->begin_preparation(view.context);
            return RunProgram{reset_};
        }
        if (phase_==1) return RunProgram{acquire_};
        const auto result=result_->read();
        if (!result || !result->claimed_slot || !result->camera_prepared
            || !Observation<bool>{Knowledge::present,true,result->context.now,result->context.window,result->context.generation}.has_value(view.context,Time(2000)))
            throw std::runtime_error("Return did not confirm a hive slot and prepared camera");
        return Done{"Returned to a confirmed hive"};
    }
    void step_completed(const View&) override { ++phase_; }
    void finished(State state) override { if (state!=State::completed) result_->reset(); }
};
class AcquireHive final : public Program {
    enum class Stage { locate,zoom,pitch_limit,pitch,observe,turn,restore,claim,prepare,recover,done };
    Stage stage_{Stage::locate};
    std::shared_ptr<const game::HiveSlotsSource> slots_;
    std::shared_ptr<const game::LandmarkSource> spawn_,reference_,proximity_;
    HiveClaimOptions options_;
    std::unique_ptr<Program> child_;
    std::unique_ptr<RunGuard> child_guard_;
    Time minimum_{},seen_{Time(-1)},child_started_{};
    std::optional<Time> child_running_;
    std::size_t child_steps_{};
    bool turned_{},issued_{};
    bool started_{},recovered_{};
    void observe(Time now) {
        stage_=Stage::observe; minimum_=now+Time(150); seen_=Time(-1);
    }
    void begin(Stage stage,const View& view) {
        stage_=stage;
        auto claim=options_; claim.search_opposite_view=false; claim.confirmed_spawn=true;
        child_=stage==Stage::recover?request_respawn(options_.recovery):stage==Stage::claim?claim_hive_from_spawn(slots_,std::move(claim))
            :prepare_hive_view(reference_,proximity_,options_.result);
        child_guard_=child_->guard(); child_started_=view.context.now;
        child_running_.reset(); child_steps_=0; issued_=false;
    }
    Step child_step(const View& view) {
        auto local=view; local.completed_steps=child_steps_;
        local.elapsed=child_running_?view.context.now-*child_running_:Time{};
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
        if (!child_running_) child_running_=view.context.now;
        if (local.elapsed>=child_->config().duration) {
            if (stage_==Stage::prepare) return recover(view);
            if (stage_==Stage::recover) throw ClientRejoinRequired("Hive reset timed out; rejoining Bee Swarm");
            throw ObservationUnavailable("Hive setup phase timed out");
        }
        Step step;
        try { step=child_->next(local); }
        catch (const HiveAlignmentError&) {
            if (stage_!=Stage::prepare) throw;
            return recover(view);
        }
        if (std::holds_alternative<Done>(step)) {
            child_->finished(State::completed); child_.reset(); child_guard_.reset();
            if (stage_==Stage::recover) {
                recovered_=true; turned_=false; stage_=Stage::locate; minimum_=view.context.now+Time(1);
                seen_=Time(-1); return Wait{};
            }
            if (stage_==Stage::claim) { begin(Stage::prepare,view); return child_step(view); }
            stage_=Stage::done; return next(view);
        }
        issued_=true;
        return step;
    }
public:
    AcquireHive(std::shared_ptr<const game::HiveSlotsSource> slots,std::shared_ptr<const game::LandmarkSource> spawn,
        std::shared_ptr<const game::LandmarkSource> reference,std::shared_ptr<const game::LandmarkSource> proximity,HiveClaimOptions options)
        :slots_(std::move(slots)),spawn_(std::move(spawn)),reference_(std::move(reference)),proximity_(std::move(proximity)),options_(options) {
        if (!slots_ || !spawn_ || !reference_ || !proximity_) throw std::invalid_argument("Hive setup requires slot, spawn and hive observations");
        if (!std::isfinite(options.speed) || options.speed<8 || options.speed>200 || options.preferred_slot>6 || options.existing_slot>6)
            throw std::invalid_argument("Invalid hive setup speed or preferred slot");
    }
    RunConfig config() const override { return {Time(240000),options_.speed,{},true,true}; }
    std::string activity_label() const override { return stage_==Stage::claim?"Claiming hive":""; }
    std::vector<Rect> text_regions() const override {
        if (child_) {
            auto regions=child_->text_regions();
            if (child_guard_) { const auto requested=child_guard_->text_regions(); regions.insert(regions.end(),requested.begin(),requested.end()); }
            return regions;
        }
        return stage_==Stage::locate || stage_==Stage::observe?std::vector<Rect>{game::hive_proximity_prompt().region}:std::vector<Rect>{};
    }
    bool expects_respawn() const noexcept override { return child_ && child_->expects_respawn(); }
    ProgramFactory recovery(Interruption reason,const View&) override {
        if (!options_.recovery || (reason!=Interruption::death && reason!=Interruption::lost_observations
            && reason!=Interruption::lost_route)) return {};
        if (reason!=Interruption::death && recovered_)
            throw ClientRejoinRequired("Hive could not be confirmed after resetting; rejoining Bee Swarm");
        if (child_) child_->finished(State::stopped);
        child_.reset(); child_guard_.reset(); child_running_.reset();
        if (options_.result) options_.result->invalidate_camera();
        stage_=Stage::locate; started_=turned_=issued_=false; recovered_=true;
        seen_=Time(-1);
        return [source=options_.recovery,reason] {
            return reason==Interruption::death?wait_for_respawn(source):request_respawn(source);
        };
    }
    void begin_recovery(const View& view) {
        if (!options_.recovery) throw ObservationUnavailable("Hive position could not be found");
        if (recovered_) throw ClientRejoinRequired("Hive could not be found after resetting; rejoining Bee Swarm");
        if (options_.result) options_.result->begin_preparation(view.context);
        begin(Stage::recover,view);
    }
    Step recover(const View& view) { begin_recovery(view); return child_step(view); }
    Step next(const View& view) override {
        if (!started_) {
            if (options_.result) {
                const auto previous=options_.result->read();
                const auto& current=view.context;
                if (previous && previous->claimed_slot && previous->camera_prepared
                    && previous->context.generation==current.generation
                    && previous->context.window.id==current.window.id
                    && previous->context.window.process==current.window.process
                    && previous->context.window.bounds==current.window.bounds
                    && current.now>=previous->context.now)
                    return Done{"Hive view is already prepared"};
                options_.result->begin_preparation(current);
            }
            minimum_=view.context.now; started_=true;
        }
        const auto& c=view.context;
        switch (stage_) {
        case Stage::claim:
        case Stage::prepare:
        case Stage::recover: return child_step(view);
        case Stage::done: {
            const auto result=options_.result?options_.result->read():std::nullopt;
            if (result && result->claimed_slot) return Done{"Hive "+std::to_string(*result->claimed_slot)+" confirmed and camera prepared"};
            throw std::runtime_error("Select your already claimed hive in Setup, or start at spawn to claim one automatically");
        }
        case Stage::zoom: return MoveCamera{{Key::zoom_in,10}};
        case Stage::pitch_limit: return MoveCamera{{Key::pitch_down,11}};
        case Stage::pitch: return MoveCamera{{Key::pitch_up,5}};
        case Stage::turn:
        case Stage::restore: return MoveCamera{{Key::rotate_right,4}};
        case Stage::locate: {
            if (c.now-minimum_>=Time(10000)) { stage_=Stage::zoom; return next(view); }
            if (!view.observations) return Wait{};
            const auto& text=view.observations->text;
            if (!text.has_value(c,Time(2000),minimum_) || text.captured<seen_) { return Wait{}; }
            if (text.captured==seen_) return Wait{};
            seen_=text.captured;
            const auto owned=game::read_prompt_landmark(c,text,game::hive_proximity_prompt());
            const auto claim=game::read_prompt_landmark(c,text,game::hive_claim_prompt());
            if (owned.value && claim.value) {
                return Wait{Time(50),"Reading hive interaction prompt"};
            }
            const int candidate=owned.value?1:claim.value?2:0;
            if (candidate==2 && options_.result) options_.result->reset();
            if (candidate==1) {
                auto result=options_.result?options_.result->read():std::nullopt;
                if (options_.existing_slot && options_.result && (!result || !result->claimed_slot)) {
                    options_.result->claimed(options_.existing_slot,c); result=options_.result->read();
                }
                if (!result || !result->claimed_slot)
                    throw std::runtime_error("Select your already claimed hive in Setup, or start at spawn to claim one automatically");
                begin(Stage::prepare,view); return child_step(view);
            }
            stage_=Stage::zoom; return next(view);
        }
        case Stage::observe: break;
        }
        if (view.observations) {
            const auto& text=view.observations->text;
            const auto owned=game::read_prompt_landmark(c,text,game::hive_proximity_prompt());
            const auto claim=game::read_prompt_landmark(c,text,game::hive_claim_prompt());
            if (owned.has_value(c,Time(2000),minimum_) && owned.value && !claim.value) {
                auto result=options_.result?options_.result->read():std::nullopt;
                if (options_.existing_slot && options_.result && (!result || !result->claimed_slot)) {
                    options_.result->claimed(options_.existing_slot,c); result=options_.result->read();
                }
                if (!result || !result->claimed_slot)
                    throw std::runtime_error("Select your already claimed hive in Setup, or start at spawn to claim one automatically");
                begin(Stage::prepare,view); return child_step(view);
            }
        }
        if (c.now-minimum_>=Time(5000)) return recover(view);
        const auto reading=spawn_->read(c);
        if (reading.usable(c,Time(2000),minimum_) && reading.captured>seen_) {
            seen_=reading.captured;
            if (reading.knowledge==Knowledge::present && reading.value) {
                begin(Stage::claim,view); return child_step(view);
            }
            if (!view.observations || !view.observations->text.has_value(c,Time(2000),minimum_))
                return Wait{Time(50),"Reading hive interaction prompt"};
            stage_=turned_?Stage::restore:Stage::turn; return next(view);
        }
        return Wait{Time(50),"Checking the settled spawn view"};
    }
    void step_completed(const View& view) override {
        switch (stage_) {
        case Stage::zoom: stage_=Stage::pitch_limit; break;
        case Stage::pitch_limit: stage_=Stage::pitch; break;
        case Stage::pitch: observe(view.context.now); break;
        case Stage::turn: turned_=true; observe(view.context.now); break;
        case Stage::restore:
            begin_recovery(view); break;
        case Stage::claim:
        case Stage::prepare:
        case Stage::recover:
            if (issued_) {
                auto local=view; local.completed_steps=++child_steps_; local.elapsed=view.context.now-*child_running_;
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
    std::shared_ptr<const game::LandmarkSource> proximity,HiveClaimOptions options) {
    return std::make_unique<AcquireHive>(std::move(slots),std::move(spawn),std::move(reference),std::move(proximity),options);
}
std::unique_ptr<Program> respawn_to_hive(std::shared_ptr<const game::HiveSlotsSource> slots,
    std::shared_ptr<const game::LandmarkSource> spawn,std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,HiveClaimOptions options) {
    if (!options.recovery) throw std::invalid_argument("Returning by respawn needs menu observations");
    auto recovery=options.recovery; options.confirmed_spawn=false;
    acquire_hive(slots,spawn,reference,proximity,options);
    return std::make_unique<RespawnToHive>([recovery] { return request_respawn(recovery); },
        [slots,spawn,reference,proximity,options] { return acquire_hive(slots,spawn,reference,proximity,options); },recovery,options.result,options.speed);
}
std::unique_ptr<Program> recover_to_hive(std::shared_ptr<const game::HiveSlotsSource> slots,
    std::shared_ptr<const game::LandmarkSource> spawn,std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,HiveClaimOptions options) {
    if (!options.recovery) throw std::invalid_argument("Death recovery needs game observations");
    options.confirmed_spawn=false;
    acquire_hive(slots,spawn,reference,proximity,options);
    return std::make_unique<RespawnToHive>([source=options.recovery] { return wait_for_respawn(source); },
        [slots,spawn,reference,proximity,options] { return acquire_hive(slots,spawn,reference,proximity,options); },options.recovery,options.result,options.speed,Reentry::respawn);
}

std::unique_ptr<Program> reconnect_to_hive(std::shared_ptr<const game::HiveSlotsSource> slots,
    std::shared_ptr<const game::LandmarkSource> spawn,std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,HiveClaimOptions options,ReconnectStart start) {
    if (!options.recovery) throw std::invalid_argument("Reconnect needs game-view observations");
    if (start!=ReconnectStart::request && start!=ReconnectStart::loading) throw std::invalid_argument("Invalid reconnect starting phase");
    options.confirmed_spawn=false;
    acquire_hive(slots,spawn,reference,proximity,options);
    return std::make_unique<RespawnToHive>([source=options.recovery,start] { return start==ReconnectStart::loading?wait_for_reconnect(source):request_reconnect(source); },
        [slots,spawn,reference,proximity,options] { return acquire_hive(slots,spawn,reference,proximity,options); },options.recovery,options.result,options.speed,Reentry::reconnect);
}

}
