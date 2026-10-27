#include "reconnect.hpp"
#include "game/detection/connection.hpp"

namespace hive::automation {
namespace {
class Reconnect final : public Program {
    std::shared_ptr<const game::RespawnSource> source_;
    Time minimum_{Time(-1)},started_{Time(-1)},last_{Time(-1)},retry_at_{};
    unsigned attempts_{};
    bool clicked_{},issued_{},resuming_{};
public:
    explicit Reconnect(std::shared_ptr<const game::RespawnSource> source,bool joining=false):source_(std::move(source)),clicked_(joining) {
        if (!source_) throw std::invalid_argument("Reconnect needs game-view observations");
    }
    RunConfig config() const override { return {game_loading_timeout+Time(15000),28,{},true,true}; }
    std::string activity_label() const override { return "Rejoining Bee Swarm"; }
    bool expects_respawn() const noexcept override { return true; }
    bool expects_reconnect() const noexcept override { return true; }
    Step next(const View& view) override {
        const auto& c=view.context;
        if (minimum_<Time{}) started_=minimum_=c.now;
        if (c.now-started_>=game_loading_timeout) throw ClientRejoinRequired("Game view did not return after waiting for reconnect");
        if (!view.observations) return Wait{Time(100),"Reading reconnect state"};
        const auto& state=*view.observations;
        const auto target=state.reconnect;
        // Joining can itself fail onto this dialog; handle it just like a
        // disconnect during play instead of waiting out the loading deadline.
        if (c.now>=retry_at_ && target.has_value(c,Time(2000),std::max(minimum_,retry_at_))) {
            if (target.captured<=last_) return Wait{Time(100),"Confirming reconnect button"};
            last_=target.captured;
            if (attempts_>=3) throw ClientRejoinRequired("Reconnect dialog remained open after three attempts");
            issued_=true;
            return UseControl{target,0,"Reconnect to game"};
        }
        const auto game_view=source_->read(c);
        if (state.connection.has_value(c,Time(2000),minimum_) && state.connection.value==game::Connection::connected
            && game_view.has_value(c,Time(2000),minimum_) && game_view.value.screen==game::RespawnScreen::menu) {
            if (c.now<retry_at_) return Wait{Time(100),"Waiting for the game menu to close"};
            issued_=resuming_=true;
            return TapMenuKey{Key::game_menu,"Resume loaded game"};
        }
        const bool hud=state.game_hud.has_value(c,Time(2000),minimum_) && state.game_hud.value;
        const bool observed_game=game_view.has_value(c,Time(2000),minimum_)
            && (game_view.value.screen==game::RespawnScreen::game);
        if ((hud || observed_game)
            && !target.has_value(c,Time(2000))
            && !(state.connection.has_value(c,Time(2000)) && state.connection.value==game::Connection::disconnected)
            && !(state.alive.has_value(c,Time(2000)) && !state.alive.value)
            ) {
            return Done{"Game view returned after reconnect"};
        }
        if (!clicked_ && c.now-minimum_>=Time(10000)) throw ClientRejoinRequired("Reconnect button or returned game view could not be confirmed");
        return Wait{Time(100),clicked_?"Waiting for game to reconnect":"Reading reconnect state"};
    }
    void step_completed(const View& view) override {
        if (!issued_) return;
        issued_=false;
        if (resuming_) { resuming_=false; minimum_=view.context.now+Time(1); retry_at_=view.context.now+Time(750); return; }
        ++attempts_; retry_at_=view.context.now+Time(3000);
        clicked_=true; minimum_=view.context.now+Time(1); last_=Time(-1);
    }
};
}
std::unique_ptr<Program> request_reconnect(std::shared_ptr<const game::RespawnSource> source) {
    return std::make_unique<Reconnect>(std::move(source));
}
std::unique_ptr<Program> wait_for_reconnect(std::shared_ptr<const game::RespawnSource> source) {
    return std::make_unique<Reconnect>(std::move(source),true);
}
}
