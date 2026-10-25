#include "state_observation.hpp"
#include "perception.hpp"
#include "respawn.hpp"
#include "connection.hpp"
#include "dialog.hpp"
#include "shift_lock.hpp"
#include "landmarks.hpp"

namespace hive::game {
namespace {
struct StateReading final : Detection { State state; };
class StateDetector final : public Detector {
    ObservationKind kind_;
    mutable HudInspector hud_;
public:
    explicit StateDetector(ObservationKind kind):kind_(kind) {}
    Rect capture_region() const override {
        return kind_==ObservationKind::health || kind_==ObservationKind::interaction?upper_hud_region:Rect{0,0,1,1};
    }
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        if (input.cancelled && input.cancelled()) return {};
        auto result=std::make_shared<StateReading>(); auto& state=result->state;
        const auto& frame=input.frame; const auto generation=input.generation;
        switch (kind_) {
        case ObservationKind::health: {
            auto health=read_empty_health_bar(frame,generation);
            const bool visible=health.knowledge==Knowledge::present && !health.value && game_hud_visible(frame,generation);
            if (health.knowledge==Knowledge::present && (health.value || visible)) {
                state.alive=health; state.alive.value=!health.value;
                if (health.value) state.last_death=health;
            }
            if (visible) state.game_hud={Knowledge::present,true,frame.captured,frame.window,generation};
            break;
        }
        case ObservationKind::connection: {
            const auto visible=game_hud_visible(frame,generation);
            const auto view=read_connection_view(frame,generation,visible);
            state.connection=view.connection; state.reconnect=view.reconnect; state.client_home=view.home;
            state.connection.captured=frame.captured; state.connection.window=frame.window; state.connection.generation=generation;
            state.game_hud={Knowledge::present,visible,frame.captured,frame.window,generation};
            break;
        }
        case ObservationKind::menus: {
            const auto hud=hud_.inspect(frame,generation,true,false);
            state.menus={Knowledge::present,hud.menus,frame.captured,frame.window,generation}; state.open_menu=hud.open_menu; break;
        }
        case ObservationKind::backpack: {
            const auto hud=hud_.inspect(frame,generation,false,true);
            state.backpack_percent=hud.backpack; state.pollen_changed_at=hud.pollen_changed_at; break;
        }
        case ObservationKind::confirmation: state.confirmation=read_confirmation(frame,generation); break;
        case ObservationKind::shift_lock:
            if (game_hud_visible(frame,generation)) state.shift_lock=read_shift_lock(frame,generation);
            break;
        case ObservationKind::interaction: state.interaction_banner=read_interaction_banner(frame,generation); break;
        }
        return result;
    }
};
}
std::vector<StateSensor> state_sensors() {
    using K=ObservationKind; using P=DetectionPriority;
    std::vector<StateSensor> result;
    auto add=[&](K kind,const char* name,Time interval,P priority=P::normal) {
        result.push_back({kind,{name,std::make_shared<StateDetector>(kind),interval,DetectionMode::pixels,false,priority}});
    };
    add(K::interaction,"state.interaction",Time(50),P::responsive);
    add(K::health,"state.health",Time(100),P::responsive);
    add(K::connection,"state.connection",Time(750));
    add(K::menus,"state.menus",Time(100));
    add(K::backpack,"state.backpack",Time(750));
    add(K::confirmation,"state.confirmation",Time(100));
    add(K::shift_lock,"state.shift-lock",Time(100));
    return result;
}
void apply_state_observation(State& state,ObservationKind kind,const Detection& result) {
    const auto& value=dynamic_cast<const StateReading&>(result).state;
    switch (kind) {
    case ObservationKind::health:
        state.alive=value.alive;
        if (value.last_death.knowledge==Knowledge::present) state.last_death=value.last_death;
        if (value.game_hud.knowledge==Knowledge::present && value.game_hud.captured>=state.game_hud.captured) state.game_hud=value.game_hud;
        break;
    case ObservationKind::connection:
        if (value.connection.captured>=state.connection.captured) state.connection=value.connection;
        state.reconnect=value.reconnect; state.client_home=value.client_home;
        if (value.game_hud.captured>=state.game_hud.captured) state.game_hud=value.game_hud;
        break;
    case ObservationKind::menus: state.menus=value.menus; state.open_menu=value.open_menu; break;
    case ObservationKind::backpack: state.backpack_percent=value.backpack_percent; state.pollen_changed_at=value.pollen_changed_at; break;
    case ObservationKind::confirmation: state.confirmation=value.confirmation; break;
    case ObservationKind::shift_lock: state.shift_lock=value.shift_lock; break;
    case ObservationKind::interaction: state.interaction_banner=value.interaction_banner; break;
    }
}
}
