#include "hive/automation/respawn.hpp"

namespace hive::automation {
namespace {
class RespawnCompletion {
    bool saw_death_{},was_away_{};
public:
    void departing(const View& view) {
        if (!view.observations) return;
        const auto& prompt=view.observations->hive_prompt;
        was_away_=prompt.usable(view.context,Time(2000)) && prompt.knowledge==Knowledge::absent;
    }
    bool ready(const View& view,const game::RespawnSource& source,Time minimum) {
        const auto& c=view.context;
        const auto* state=view.observations;
        if (state && state->last_death.has_value(c,Time(25000),minimum) && state->last_death.value) saw_death_=true;
        const bool health=state && state->alive.has_value(c,Time(2000),minimum);
        const bool hud=state && state->game_hud.has_value(c,Time(2000),minimum);
        if (health && !state->alive.value) { saw_death_=true; return false; }
        if (was_away_ && state && state->at_hive.has_value(c,Time(2000),minimum) && state->at_hive.value) return true;
        bool returned=false;
        if (hud && state->game_hud.value && health) {
            returned=true;
        } else {
            const auto reading=source.read(c);
            if (reading.has_value(c,Time(2000),minimum)) {
                saw_death_|=reading.value.screen==game::RespawnScreen::respawning;
                returned=reading.value.screen==game::RespawnScreen::game || reading.value.screen==game::RespawnScreen::controls;
            }
        }
        return returned && (saw_death_ || c.now-minimum>=Time(8000));
    }
};
class AwaitRespawn final : public Program {
    std::shared_ptr<const game::RespawnSource> source_;
    Time started_{Time(-1)};
    RespawnCompletion completion_;
public:
    explicit AwaitRespawn(std::shared_ptr<const game::RespawnSource> source):source_(std::move(source)) {
        if (!source_) throw std::invalid_argument("Respawn wait needs game observations");
    }
    RunConfig config() const override { return {Time(25000),28,{},true,true}; }
    bool expects_respawn() const noexcept override { return true; }
    Step next(const View& view) override {
        if (started_<Time{}) started_=view.context.now;
        if (completion_.ready(view,*source_,started_)) return Done{"Game view returned after respawn"};
        return Wait{Time(100),"Waiting for player respawn"};
    }
};
class Respawn final : public Program {
    enum class Stage { locate,select,confirm,transition,done };
    std::shared_ptr<const game::RespawnSource> source_;
    Stage stage_{Stage::locate};
    Time started_{Time(-1)},minimum_{};
    RespawnCompletion completion_;
    bool issued_{};
public:
    explicit Respawn(std::shared_ptr<const game::RespawnSource> source):source_(std::move(source)) {
        if (!source_) throw std::invalid_argument("Respawn needs game observations");
    }
    RunConfig config() const override { return {Time(35000),28,{},false,true}; }
    class Guard final : public RunGuard {
    public:
        bool needs_ocr() const noexcept override { return true; }
        GuardDecision evaluate(const View&,Time) override { return {}; }
    };
    std::unique_ptr<RunGuard> guard() const override { return std::make_unique<Guard>(); }
    bool expects_respawn() const noexcept override { return stage_!=Stage::done; }
    Step next(const View& view) override {
        const auto& c=view.context;
        if (started_<Time{}) started_=minimum_=c.now;
        if (stage_==Stage::done) return Done{"Respawn finished; confirming the hive"};
        if (stage_==Stage::locate && view.observations && view.observations->alive.has_value(c,Time(2000),minimum_)
            && !view.observations->alive.value) {
            stage_=Stage::transition; started_=minimum_=c.now;
        }
        if (c.now-started_>=(stage_==Stage::transition?Time(25000):Time(8000)))
            throw std::runtime_error(stage_==Stage::transition?"Respawn transition timed out":"Respawn menu could not be confirmed");
        if (stage_==Stage::transition) {
            if (!completion_.ready(view,*source_,minimum_)) return Wait{Time(100),"Waiting for player respawn"};
            stage_=Stage::done; return next(view);
        }
        if (stage_==Stage::select) {
            stage_=Stage::confirm; issued_=true;
            return TapMenuKey{Key::respawn,"Select respawn"};
        }
        if (stage_==Stage::confirm) {
            stage_=Stage::transition; issued_=true;
            return TapMenuKey{Key::confirm,"Confirm respawn"};
        }
        const auto reading=source_->read(c);
        if (!reading.has_value(c,Time(2000),minimum_)) {
            if (stage_==Stage::locate && view.observations
                && view.observations->text.has_value(c,Time(2000),minimum_)) {
                stage_=Stage::select; issued_=true;
                return TapMenuKey{Key::game_menu,"Open game menu"};
            }
            return Wait{Time(50),"Waiting for respawn menu"};
        }
        const auto screen=reading.value.screen;
        if (screen==game::RespawnScreen::game) completion_.departing(view);
        if (screen==game::RespawnScreen::respawning) {
            stage_=Stage::transition; started_=minimum_=c.now;
            return next(view);
        }
        if (screen==game::RespawnScreen::confirmation) {
            stage_=Stage::transition; issued_=true;
            return TapMenuKey{Key::confirm,"Confirm respawn"};
        }
        if (screen==game::RespawnScreen::menu) {
            stage_=Stage::confirm; issued_=true;
            if (!reading.value.target) return TapMenuKey{Key::respawn,"Select respawn"};
            return UseControl{{Knowledge::present,*reading.value.target,reading.captured,reading.window,reading.generation},0,"Select respawn"};
        }
        stage_=Stage::select; issued_=true;
        return TapMenuKey{Key::game_menu,"Open game menu"};
    }
    void step_completed(const View& view) override {
        if (!issued_) return;
        issued_=false; minimum_=view.context.now+Time(1);
        if (stage_==Stage::transition) started_=view.context.now;
    }
};
}
std::unique_ptr<Program> request_respawn(std::shared_ptr<const game::RespawnSource> source) { return std::make_unique<Respawn>(std::move(source)); }
std::unique_ptr<Program> wait_for_respawn(std::shared_ptr<const game::RespawnSource> source) { return std::make_unique<AwaitRespawn>(std::move(source)); }
}
