#include "travel_preparation.hpp"
#include "game/detection/menus.hpp"
#include "game/actions/interaction/ant_amulet.hpp"

namespace hive::automation {
namespace {
class Preparation final : public Program {
    ProgramFactory movement_,hive_;
    TravelArrival arrival_;
    double speed_;
    bool convert_backpack_;
    unsigned conversion_retries_{};
    enum class Phase { arrival,amulet,park,movement,hive,converting,done } phase_;
public:
    Preparation(ProgramFactory movement,ProgramFactory hive,double speed,bool convert_backpack,TravelArrival arrival)
        :movement_(std::move(movement)),hive_(std::move(hive)),arrival_(std::move(arrival)),speed_(speed),convert_backpack_(convert_backpack),
        phase_(arrival_.prepare?Phase::arrival:Phase::park) {
        if (!hive_ || !std::isfinite(speed_) || speed_<8 || speed_>200)
            throw std::invalid_argument("Travel preparation needs hive setup and a valid movement speed");
    }
    RunConfig config() const override { return {std::chrono::minutes(30),speed_,{},true,true}; }
    bool expects_reconnect() const noexcept override { return phase_==Phase::arrival && (!arrival_.pending || arrival_.pending()); }
    bool expects_respawn() const noexcept override { return expects_reconnect(); }
    ActivityStatus activity_status() const override {
        if (convert_backpack_ && (phase_==Phase::converting || phase_==Phase::done)) return {"Converting","Backpack"};
        return {};
    }
    ProgramFactory recovery(Interruption reason,const View&) override {
        if (reason!=Interruption::lost_observations || phase_!=Phase::converting || conversion_retries_>=2) return {};
        ++conversion_retries_;
        return hive_;
    }
    bool resume_after_reconnect(const View&) override {
        phase_=Phase::park; conversion_retries_=0;
        return true;
    }
    Step next(const View& view) override {
        if (phase_==Phase::arrival) {
            if (expects_reconnect()) return RunProgram{arrival_.prepare};
            // Reconnect performs this cleanup itself. Starting in an already
            // loaded game must also clear a restored Ant result before setup.
            phase_=Phase::amulet;
        }
        if (phase_==Phase::amulet) return RunProgram{keep_ant_amulet};
        if (phase_==Phase::park)
            return Hold{{},Time(1),game::pointer_rest(view.context,view.observations?*view.observations:game::State{})};
        if (phase_==Phase::movement) {
            if (movement_) return RunProgram{movement_};
            phase_=Phase::hive;
        }
        if (phase_==Phase::hive) return RunProgram{hive_};
        if (phase_==Phase::converting) return Convert{};
        return Done{"Travel preparation complete"};
    }
    void step_completed(const View&) override {
        if (phase_==Phase::arrival || phase_==Phase::amulet) phase_=Phase::park;
        else if (phase_==Phase::park) phase_=Phase::movement;
        else if (phase_==Phase::movement) phase_=Phase::hive;
        else if (phase_==Phase::hive) phase_=convert_backpack_?Phase::converting:Phase::done;
        else if (phase_==Phase::converting) phase_=Phase::done;
    }
};
}
std::unique_ptr<Program> prepare_travel(ProgramFactory movement,ProgramFactory hive,double speed,bool convert_backpack,TravelArrival arrival) {
    return std::make_unique<Preparation>(std::move(movement),std::move(hive),speed,convert_backpack,std::move(arrival));
}
}
