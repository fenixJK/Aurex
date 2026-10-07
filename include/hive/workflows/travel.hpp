#pragma once
#include "hive/game/movement/navigation.hpp"

namespace hive::workflows {
enum class TravelStage { idle, locating, travelling, confirming, arrived, failed, stopped };
enum class TravelFailure { none, location, timeout, unavailable, exhausted, context, input, interrupted, conflict };
struct TravelLeg {
    std::uint64_t ticket{};
    game::Route route;
};
struct TravelLimits {
    Time location_timeout{5000};
    Time leg_timeout{std::chrono::minutes(30)};
    std::size_t legs{64}, failures{3};
};
using PositionBasis=game::PositionBasis;
using TravelPosition=game::RoutePosition;
class Travel {
public:
    explicit Travel(game::RouteNetwork network,TravelLimits limits={});
    void start(std::string destination,const ObservationContext& context);
    std::optional<TravelLeg> update(const ObservationContext& context,const Observation<std::string>& location,
        const std::set<std::string>& available={},const std::set<std::string>& blocked={},
        const std::optional<TravelPosition>& departure={});
    bool finished(std::uint64_t ticket,const ObservationContext& context,bool executed,const Observation<std::string>& arrival={});
    void stop();
    TravelStage stage() const { return stage_; }
    const std::string& reason() const { return reason_; }
    TravelFailure failure() const { return failure_; }
    const std::set<std::string>& failed_routes() const { return failed_routes_; }
    const std::optional<TravelPosition>& position() const { return position_; }
private:
    game::RouteNetwork network_;
    TravelLimits limits_;
    ObservationContext started_;
    TravelStage stage_{TravelStage::idle};
    TravelFailure failure_{TravelFailure::none};
    std::string destination_,reason_;
    std::set<std::string> failed_routes_;
    std::optional<TravelLeg> active_;
    Time last_{}, phase_started_{}, minimum_{};
    std::uint64_t next_ticket_{};
    std::size_t legs_{}, failures_{};
    bool executed_{};
    std::optional<TravelPosition> position_;
    bool valid_context(const ObservationContext& context);
    void fail(TravelFailure failure,std::string reason);
};
}
