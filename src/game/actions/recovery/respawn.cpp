#include "respawn.hpp"
#include <algorithm>

namespace hive::automation {
namespace {
class RespawnCompletion {
    bool transitioned_{},away_from_hive_{},origin_known_{};
    Time last_dead_{Time(-1)},origin_before_{Time(-1)};
    void died(Time captured) {
        transitioned_=true;
        last_dead_=std::max(last_dead_,captured);
    }
public:
    explicit RespawnCompletion(bool already_died=false):transitioned_(already_died) {}
    void begin(Time now) { origin_before_=now; }
    bool transitioned() const { return transitioned_; }
    void observe_origin(const View& view) {
        const auto* state=view.observations;
        // A pre-reset capture may finish processing after Escape. A missing
        // prompt in the reset menu is not evidence of having been in a field.
        if (!origin_known_ && state && state->at_hive.usable(view.context,Time(2000))
            && state->at_hive.captured<=origin_before_) {
            away_from_hive_=!state->at_hive.value; origin_known_=true;
        }
    }
    bool ready(const View& view,const Observation<game::RespawnView>& reading,Time minimum,Time transition) {
        const auto& c=view.context;
        const auto* state=view.observations;
        if (state && state->last_death.has_value(c,Time(25000),transition) && state->last_death.value)
            died(state->last_death.captured);
        // Death can be captured while Enter is still being executed. Keep that
        // evidence from this attempt; only revival must follow input completion.
        if (state && state->alive.has_value(c,Time(2000),transition) && !state->alive.value)
            died(state->alive.captured);
        if (reading.has_value(c,Time(2000),transition) && reading.value.screen==game::RespawnScreen::respawning)
            died(reading.captured);
        const bool health=state && state->alive.has_value(c,Time(2000),minimum);
        const bool observed=reading.has_value(c,Time(2000),minimum);
        if (observed && (reading.value.screen==game::RespawnScreen::menu || reading.value.screen==game::RespawnScreen::confirmation))
            return false;
        if (state && state->connection.has_value(c,Time(2000)) && state->connection.value==game::Connection::disconnected) return false;
        if (health && !state->alive.value) return false;
        const auto after_death=std::max(minimum,last_dead_+Time(1));
        const bool living=health && state->alive.value && state->alive.captured>last_dead_;
        const bool hud=state && state->game_hud.has_value(c,Time(2000),after_death) && state->game_hud.value;
        const bool game=observed && reading.captured>last_dead_ && reading.value.screen==game::RespawnScreen::game;
        // The shared hive prompt may disappear and return during respawn even
        // when capture misses the empty health bar. Menu-covered frames cannot
        // establish departure, and the menu observation must be at least as new.
        if (game && hud && living && state->at_hive.usable(c,Time(2000),minimum)
            && state->at_hive.captured<=reading.captured && !state->at_hive.value)
            away_from_hive_=true;
        // Closing the confirmation can also mean cancellation or a missed key.
        // Require death/revival or a new hive arrival, never elapsed time alone.
        return ((hud && living) || game) && (transitioned_
            || (away_from_hive_ && state && state->at_hive.has_value(c,Time(2000),after_death) && state->at_hive.value));
    }
};
class AwaitRespawn final : public Program {
    std::shared_ptr<const game::RespawnSource> source_;
    std::shared_ptr<RespawnResult> result_;
    Time started_{Time(-1)};
    RespawnCompletion completion_{true};
public:
    AwaitRespawn(std::shared_ptr<const game::RespawnSource> source,std::shared_ptr<RespawnResult> result)
        :source_(std::move(source)),result_(std::move(result)) {
        if (!source_) throw std::invalid_argument("Respawn wait needs game observations");
        if (result_) *result_={};
    }
    RunConfig config() const override { return {Time(30000),28,{},true,true}; }
    bool expects_respawn() const noexcept override { return true; }
    Step next(const View& view) override {
        if (started_<Time{}) started_=view.context.now;
        if (completion_.ready(view,source_->read(view.context),started_,started_)) {
            if (result_) result_->transition={Knowledge::present,true,view.context.now,view.context.window,view.context.generation};
            return Done{"Game view returned after respawn"};
        }
        if (view.context.now-started_>=Time(25000)) throw ClientRejoinRequired("Respawn did not return the game view; rejoining Bee Swarm");
        return Wait{Time(100),"Waiting for player respawn"};
    }
};
class Respawn final : public Program {
    enum class Stage { locate,select,confirm,transition,done };
    std::shared_ptr<const game::RespawnSource> source_;
    std::shared_ptr<RespawnResult> result_;
    Stage stage_{Stage::locate};
    Time started_{Time(-1)},minimum_{},transition_{Time(-1)};
    RespawnCompletion completion_;
    bool issued_{};
    bool retried_{};
    unsigned shortcut_{};
    unsigned key_retries_{};
    std::optional<game::RespawnScreen> requested_from_;
    Step complete(const View& view,bool confirmed) {
        if (result_) result_->transition={Knowledge::present,confirmed,view.context.now,
            view.context.window,view.context.generation};
        stage_=Stage::done;
        return Done{confirmed?"Respawn finished; confirming the hive":"Reset transition unseen; checking the return location"};
    }
    Step retry(Time now,std::string reason) {
        if (retried_) throw ClientRejoinRequired("Reset could not be verified after retrying; rejoining Bee Swarm");
        retried_=true; stage_=Stage::locate; started_=minimum_=now; transition_=Time(-1);
        requested_from_.reset(); key_retries_=0; shortcut_=0; completion_=RespawnCompletion{};
        return ReportActivity{{"Retrying reset",std::move(reason)}};
    }
public:
    Respawn(std::shared_ptr<const game::RespawnSource> source,std::shared_ptr<RespawnResult> result)
        :source_(std::move(source)),result_(std::move(result)) {
        if (!source_) throw std::invalid_argument("Respawn needs game observations");
        if (result_) *result_={};
    }
    RunConfig config() const override { return {Time(50000),28,{},true,true}; }
    std::vector<Rect> text_regions() const override {
        return shortcut_<3?std::vector<Rect>{}:std::vector<Rect>{game::hive_proximity_prompt().region};
    }
    bool expects_respawn() const noexcept override { return stage_!=Stage::done; }
    Step next(const View& view) override {
        const auto& c=view.context;
        if (started_<Time{}) {
            started_=minimum_=c.now;
        }
        if (stage_==Stage::locate) completion_.begin(c.now);
        completion_.observe_origin(view);
        if (stage_==Stage::done) return Done{"Respawn finished; confirming the hive"};
        if (shortcut_<3 && view.observations && view.observations->alive.has_value(c,Time(2000))
            && !view.observations->alive.value) {
            stage_=Stage::transition; shortcut_=3; started_=c.now; transition_=view.observations->alive.captured;
        }
        // The first attempt is the normal keyboard shortcut. Do not wait for
        // captures or button recognition between its fast taps. Observation
        // below is for completion and for retrying a screen left open by lag.
        if (shortcut_<3) {
            issued_=true;
            switch (shortcut_++) {
            case 0:
                stage_=Stage::select; requested_from_=game::RespawnScreen::game;
                return TapMenuKey{Key::game_menu,"Open game menu"};
            case 1:
                stage_=Stage::confirm; requested_from_=game::RespawnScreen::menu;
                return TapMenuKey{Key::respawn,"Select respawn"};
            default:
                stage_=Stage::transition; transition_=c.now; requested_from_=game::RespawnScreen::confirmation;
                return TapMenuKey{Key::confirm,"Confirm respawn"};
            }
        }
        const auto reading=source_->read(c);
        const bool fresh=reading.has_value(c,Time(2000),minimum_);
        const auto* state=view.observations;
        const bool live=state && state->alive.has_value(c,Time(2000),minimum_) && state->alive.value
            && state->game_hud.has_value(c,Time(2000),minimum_) && state->game_hud.value
            && !(state->connection.has_value(c,Time(2000)) && state->connection.value==game::Connection::disconnected);
        const bool menu=fresh && (reading.value.screen==game::RespawnScreen::menu
            || reading.value.screen==game::RespawnScreen::confirmation);
        // Each observed menu transition gets its own response deadline. A slow
        // Escape menu must not consume the time allowed for R and Enter.
        if (menu && requested_from_!=reading.value.screen) started_=c.now;
        if (stage_!=Stage::transition && fresh && reading.value.screen==game::RespawnScreen::respawning) {
            stage_=Stage::transition; if (transition_<Time{}) transition_=reading.captured;
        }
        if (stage_==Stage::transition && !menu) {
            if (completion_.ready(view,reading,minimum_,transition_)) {
                return complete(view,true);
            }
            if (!completion_.transitioned() && !fresh && c.now-minimum_>=Time(8000))
                throw ClientRejoinRequired("No fresh reset-menu view was available after the shortcut");
            if (!completion_.transitioned() && live && fresh && reading.value.screen==game::RespawnScreen::game
                && c.now-transition_>=Time(8000)) {
                // A reset at the unclaimed spawn platform has no hive prompt
                // to return. Let the hive workflow survey the location before
                // spending another reset, without claiming death was observed.
                if (result_) return complete(view,false);
                return retry(c.now,"Game returned without a confirmed respawn");
            }
            if (c.now-transition_>=Time(25000)) throw ClientRejoinRequired("Respawn did not return a readable live game view");
            return Wait{Time(100),"Waiting for player respawn"};
        }
        if (c.now-started_>=Time(8000)) {
            if ((stage_==Stage::select || stage_==Stage::confirm) && live && fresh && reading.value.screen==game::RespawnScreen::game)
                return retry(c.now,"Respawn controls did not appear after the shortcut");
            if (!fresh) throw ClientRejoinRequired("No fresh reset-menu view was available"+
                (state && !state->observation_error.empty()?": "+state->observation_error:std::string{}));
            if (reading.value.screen==game::RespawnScreen::menu)
                throw ClientRejoinRequired("Respawn shortcut did not open the confirmation");
            if (reading.value.screen==game::RespawnScreen::confirmation)
                throw ClientRejoinRequired("Respawn confirmation did not respond");
            throw ClientRejoinRequired("Respawn menu could not be confirmed");
        }
        if (!fresh) return Wait{Time(50),"Waiting for respawn menu"};
        const auto screen=reading.value.screen;
        // The HUD can show through a menu animation. Do not toggle Escape
        // again while waiting for the menu or confirmation to finish opening.
        if (screen==game::RespawnScreen::game)
            return Wait{Time(50),"Waiting for respawn controls"};
        // R and Enter may be retried while their own controls remain visible.
        if (requested_from_==screen && c.now-minimum_<Time(750))
            return Wait{Time(50),"Waiting for respawn menu response"};
        // Keep the first press instantaneous. Only a control that remains
        // visible after a missed input gets a longer press, capped at 100 ms.
        key_retries_=requested_from_==screen?std::min(key_retries_+1,2u):0;
        const auto press=Time(50*key_retries_);
        requested_from_=screen;
        if (screen==game::RespawnScreen::confirmation) {
            // Retrying Enter must not move the start of this reset attempt
            // past a death frame that was captured while the menu was fading.
            if (transition_<Time{}) transition_=c.now;
            stage_=Stage::transition; issued_=true;
            return TapMenuKey{Key::confirm,"Confirm respawn",press};
        }
        if (screen==game::RespawnScreen::menu) {
            stage_=Stage::confirm; issued_=true;
            return TapMenuKey{Key::respawn,"Select respawn",press};
        }
        return Wait{Time(50),"Waiting for respawn controls"};
    }
    void step_completed(const View& view) override {
        if (!issued_) return;
        issued_=false; minimum_=view.context.now;
    }
};
}
std::unique_ptr<Program> request_respawn(std::shared_ptr<const game::RespawnSource> source,std::shared_ptr<RespawnResult> result) {
    return std::make_unique<Respawn>(std::move(source),std::move(result));
}
std::unique_ptr<Program> wait_for_respawn(std::shared_ptr<const game::RespawnSource> source,std::shared_ptr<RespawnResult> result) {
    return std::make_unique<AwaitRespawn>(std::move(source),std::move(result));
}
}
