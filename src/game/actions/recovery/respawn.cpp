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
    bool origin_known() const { return origin_known_; }
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
    bool ready(const View& view,const game::RespawnSource& source,Time minimum,Time transition) {
        const auto& c=view.context;
        const auto* state=view.observations;
        if (state && state->last_death.has_value(c,Time(25000),transition) && state->last_death.value)
            died(state->last_death.captured);
        const bool health=state && state->alive.has_value(c,Time(2000),minimum);
        const auto reading=source.read(c);
        const bool observed=reading.has_value(c,Time(2000),minimum);
        if (observed && (reading.value.screen==game::RespawnScreen::menu || reading.value.screen==game::RespawnScreen::confirmation))
            return false;
        if (state && state->connection.has_value(c,Time(2000)) && state->connection.value==game::Connection::disconnected) return false;
        if (health && !state->alive.value) {
            died(state->alive.captured); return false;
        }
        if (observed && reading.value.screen==game::RespawnScreen::respawning) died(reading.captured);
        const auto after_death=std::max(minimum,last_dead_+Time(1));
        const bool living=health && state->alive.value && state->alive.captured>last_dead_;
        const bool hud=state && state->game_hud.has_value(c,Time(2000),after_death) && state->game_hud.value;
        const bool game=observed && reading.captured>last_dead_ && reading.value.screen==game::RespawnScreen::game;
        // Closing the confirmation can also mean cancellation or a missed key.
        // Require death/revival or a new hive arrival, never elapsed time alone.
        return ((hud && living) || game) && (transitioned_
            || (away_from_hive_ && state && state->at_hive.has_value(c,Time(2000),after_death) && state->at_hive.value));
    }
};
class AwaitRespawn final : public Program {
    std::shared_ptr<const game::RespawnSource> source_;
    Time started_{Time(-1)};
    RespawnCompletion completion_{true};
public:
    explicit AwaitRespawn(std::shared_ptr<const game::RespawnSource> source):source_(std::move(source)) {
        if (!source_) throw std::invalid_argument("Respawn wait needs game observations");
    }
    RunConfig config() const override { return {Time(30000),28,{},true,true}; }
    bool expects_respawn() const noexcept override { return true; }
    Step next(const View& view) override {
        if (started_<Time{}) started_=view.context.now;
        if (completion_.ready(view,*source_,started_,started_)) return Done{"Game view returned after respawn"};
        if (view.context.now-started_>=Time(25000)) throw ClientRejoinRequired("Respawn did not return the game view; rejoining Bee Swarm");
        return Wait{Time(100),"Waiting for player respawn"};
    }
};
class Respawn final : public Program {
    enum class Stage { locate,select,confirm,transition,done };
    std::shared_ptr<const game::RespawnSource> source_;
    Stage stage_{Stage::locate};
    Time started_{Time(-1)},minimum_{},transition_{};
    RespawnCompletion completion_;
    bool issued_{};
    bool retried_{};
    std::optional<game::RespawnScreen> requested_from_;
    Step retry(Time now,std::string reason) {
        if (retried_) throw ObservationUnavailable("Reset could not be verified after retrying; position is unconfirmed");
        retried_=true; stage_=Stage::locate; started_=minimum_=now;
        requested_from_.reset(); completion_=RespawnCompletion{};
        return ReportActivity{{"Retrying reset",std::move(reason)}};
    }
public:
    explicit Respawn(std::shared_ptr<const game::RespawnSource> source):source_(std::move(source)) {
        if (!source_) throw std::invalid_argument("Respawn needs game observations");
    }
    RunConfig config() const override { return {Time(50000),28,{},true,true}; }
    std::vector<Rect> text_regions() const override { return {game::hive_proximity_prompt().region}; }
    bool expects_respawn() const noexcept override { return stage_!=Stage::done; }
    Step next(const View& view) override {
        const auto& c=view.context;
        if (started_<Time{}) {
            started_=minimum_=c.now;
        }
        if (stage_==Stage::locate) completion_.begin(c.now);
        completion_.observe_origin(view);
        if (stage_==Stage::done) return Done{"Respawn finished; confirming the hive"};
        if (stage_==Stage::locate && view.observations && view.observations->alive.has_value(c,Time(2000),minimum_)
            && !view.observations->alive.value) {
            stage_=Stage::transition; started_=c.now; transition_=view.observations->alive.captured;
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
        if (stage_==Stage::transition && !menu) {
            if (completion_.ready(view,*source_,minimum_,transition_)) {
                stage_=Stage::done; return next(view);
            }
            if (!completion_.transitioned() && live && fresh && reading.value.screen==game::RespawnScreen::game
                && c.now-transition_>=Time(8000))
                return retry(c.now,"Game returned without a confirmed respawn");
            if (c.now-transition_>=Time(25000)) throw ClientRejoinRequired("Respawn did not return a readable live game view");
            return Wait{Time(100),"Waiting for player respawn"};
        }
        if (fresh && reading.value.screen==game::RespawnScreen::respawning) {
            stage_=Stage::transition; transition_=reading.captured;
            return next(view);
        }
        if (c.now-started_>=Time(8000)) {
            if ((stage_==Stage::select || stage_==Stage::confirm) && live && fresh && reading.value.screen==game::RespawnScreen::game)
                return retry(c.now,"Respawn controls did not appear after the shortcut");
            throw ClientRejoinRequired("Respawn menu could not be confirmed");
        }
        if (stage_==Stage::locate && !completion_.origin_known() && c.now-started_<Time(1000)
            && (!reading.has_value(c,Time(2000),minimum_) || reading.value.screen==game::RespawnScreen::game))
            return Wait{Time(50),"Recording location before reset"};
        if (!fresh) {
            if (stage_==Stage::locate && view.observations
                && ((view.observations->game_hud.has_value(c,Time(2000),minimum_) && view.observations->game_hud.value)
                    || view.observations->text.has_value(c,Time(2000),minimum_))) {
                stage_=Stage::select; issued_=true; requested_from_=game::RespawnScreen::game;
                return TapMenuKey{Key::game_menu,"Open game menu"};
            }
            return Wait{Time(50),"Waiting for respawn menu"};
        }
        const auto screen=reading.value.screen;
        // The HUD can show through a menu animation. Do not toggle Escape
        // again while waiting for the menu or confirmation to finish opening.
        if (screen==game::RespawnScreen::game && stage_!=Stage::locate)
            return Wait{Time(50),"Waiting for respawn controls"};
        // R and Enter may be retried while their own controls remain visible.
        if (requested_from_==screen && c.now-minimum_<Time(750))
            return Wait{Time(50),"Waiting for respawn menu response"};
        requested_from_=screen;
        if (screen==game::RespawnScreen::confirmation) {
            stage_=Stage::transition; transition_=c.now; issued_=true;
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
        issued_=false; minimum_=view.context.now;
    }
};
}
std::unique_ptr<Program> request_respawn(std::shared_ptr<const game::RespawnSource> source) { return std::make_unique<Respawn>(std::move(source)); }
std::unique_ptr<Program> wait_for_respawn(std::shared_ptr<const game::RespawnSource> source) { return std::make_unique<AwaitRespawn>(std::move(source)); }
}
