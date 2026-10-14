#include "travel.hpp"
#include <algorithm>
#include <limits>

namespace hive::workflows {
Travel::Travel(game::RouteNetwork network,TravelLimits limits):network_(std::move(network)),limits_(limits) {
    if (limits.location_timeout<Time(100) || limits.location_timeout>Time(60000)
        || limits.leg_timeout<Time(100) || limits.leg_timeout>std::chrono::minutes(30)
        || !limits.legs || limits.legs>256 || !limits.failures || limits.failures>32)
        throw std::invalid_argument("Invalid travel limits");
}
void Travel::start(std::string destination,const ObservationContext& context) {
    if (stage_==TravelStage::travelling || stage_==TravelStage::confirming || stage_==TravelStage::locating)
        throw std::logic_error("Travel is already active");
    if (!network_.contains(destination) || !context.generation || !context.window.id || !context.window.bounds.valid()
        || context.now<Time::zero()) throw std::invalid_argument("Travel needs a destination and valid session");
    destination_=std::move(destination); started_=context;
    last_=phase_started_=minimum_=context.now;
    active_.reset(); position_.reset(); failed_routes_.clear(); reason_.clear(); legs_=failures_=0;
    stage_=TravelStage::locating; failure_=TravelFailure::none;
}
void Travel::fail(TravelFailure failure,std::string reason) {
    if (failure==TravelFailure::timeout || failure==TravelFailure::location) reject_route();
    failure_=failure; stage_=TravelStage::failed; reason_=std::move(reason); active_.reset(); position_.reset();
}
void Travel::reject_route() {
    if (active_ && failed_routes_.insert(active_->route.id).second) {
        network_.failed(active_->route.id,last_); ++failures_;
    }
}
void Travel::stop() { stage_=TravelStage::stopped; reason_="Travel stopped"; active_.reset(); position_.reset(); }
bool Travel::valid_context(const ObservationContext& c) {
    if (c.now<last_ || c.generation!=started_.generation || c.window.id!=started_.window.id
        || c.window.process!=started_.window.process || c.window.bounds!=started_.window.bounds) {
        fail(TravelFailure::context,"Travel context changed"); return false;
    }
    last_=c.now; return true;
}
bool Travel::finished(std::uint64_t ticket,const ObservationContext& c,bool executed,const Observation<std::string>& arrival) {
    if (stage_!=TravelStage::travelling || !active_ || active_->ticket!=ticket) return false;
    if (!valid_context(c)) return false;
    if (!executed) reject_route();
    if (c.now-phase_started_>=std::min(limits_.leg_timeout,active_->route.timeout)) { fail(TravelFailure::timeout,"Travel leg timed out"); return false; }
    if (executed && arrival.value==active_->route.to && arrival.has_value(c,Time(2000),phase_started_))
        position_=TravelPosition{arrival.value,PositionBasis::observed,{arrival.window,arrival.generation,arrival.captured}};
    executed_=executed; phase_started_=c.now; minimum_=c.now;
    stage_=TravelStage::confirming;
    return true;
}
std::optional<TravelLeg> Travel::update(const ObservationContext& c,const Observation<std::string>& location,
    const std::set<std::string>& available,const std::set<std::string>& blocked,const std::optional<TravelPosition>& departure) {
    if (stage_!=TravelStage::locating && stage_!=TravelStage::travelling && stage_!=TravelStage::confirming) return {};
    if (!valid_context(c)) return {};
    if (stage_==TravelStage::travelling) {
        if (blocked.contains(active_->route.id)
            || !std::includes(available.begin(),available.end(),active_->route.requirements.begin(),active_->route.requirements.end()))
            fail(TravelFailure::unavailable,"Active route is no longer available");
        else if (c.now-phase_started_>=std::min(limits_.leg_timeout,active_->route.timeout)) fail(TravelFailure::timeout,"Travel leg timed out");
        return {};
    }
    if (c.now-phase_started_>=limits_.location_timeout) { fail(TravelFailure::location,"Travel location could not be confirmed"); return {}; }
    const bool carried=stage_==TravelStage::locating && departure && departure->usable(c) && network_.contains(departure->location);
    const bool observed=location.has_value(c,Time(2000),carried?departure->context.now:minimum_) && network_.contains(location.value)
        && (!carried || location.captured>=departure->context.now)
        && (stage_!=TravelStage::confirming || location.captured>minimum_);
    const bool confirmed=stage_==TravelStage::confirming && executed_ && position_ && position_->basis==PositionBasis::observed
        && position_->usable(c) && position_->location==active_->route.to;
    const bool estimated=stage_==TravelStage::confirming && executed_ && active_->route.arrival==game::RouteArrival::estimated;
    if (!observed && !estimated && !carried && !confirmed) return {};
    if (observed) position_=TravelPosition{location.value,PositionBasis::observed,{location.window,location.generation,location.captured}};
    else if (carried) position_=departure;
    else if (!confirmed) position_=TravelPosition{active_->route.to,PositionBasis::route_execution,{c.window,c.generation,minimum_}};
    const auto at=position_->location;
    if (stage_==TravelStage::confirming) {
        if (!executed_ || at!=active_->route.to) {
            reject_route();
        } else network_.succeeded(active_->route.id);
        active_.reset();
    }
    if (at==destination_) { stage_=TravelStage::arrived; reason_=position_->basis==PositionBasis::observed?"Destination confirmed":"Route completed; destination estimated"; return {}; }
    if (failures_>=limits_.failures || legs_>=limits_.legs) { fail(TravelFailure::exhausted,"Travel recovery limit reached"); return {}; }
    auto excluded=blocked; excluded.insert(failed_routes_.begin(),failed_routes_.end());
    std::optional<game::Journey> journey;
    try { journey=network_.plan(at,destination_,available,excluded,c.now); }
    catch (const std::invalid_argument& error) { fail(TravelFailure::unavailable,error.what()); return {}; }
    if (!journey) { fail(failures_?TravelFailure::exhausted:TravelFailure::unavailable,"No available route to destination"); return {}; }
    if (next_ticket_==std::numeric_limits<std::uint64_t>::max()) { fail(TravelFailure::exhausted,"Travel ticket limit reached"); return {}; }
    active_=TravelLeg{++next_ticket_,network_.route(journey->routes.front())}; ++legs_;
    position_.reset(); stage_=TravelStage::travelling; phase_started_=c.now;
    return active_;
}
}
