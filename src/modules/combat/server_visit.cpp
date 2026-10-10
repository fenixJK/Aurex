#include "server_visit.hpp"

namespace hive::features::combat {
void MondoTrip::configure(bool selected,int lead,bool interrupt) {
    selected_=selected; lead_=lead; interrupt_=interrupt;
    if (!selected) { enabled_=false; phase_=Phase::idle; }
}
void MondoTrip::update(const extensions::SessionView& view,Time wall,bool due) {
    now_=view.now; generation_=view.generation;
    if (session_!=view.session_id) { session_=view.session_id; phase_=Phase::idle; cycle_=Time(-1); }
    enabled_=selected_ && !view.server.configured_target.empty() && view.server.configured_target!=game::JoinTarget{}.uri();
    if (!enabled_ || view.status.state!=RunState::running) return;
    const auto hour=Time(std::chrono::hours(1));
    const auto cycle=(wall+Time(lead_*1000))/hour*hour;
    const bool window=wall>=cycle-Time(lead_*1000) && wall<cycle+std::chrono::minutes(15);
    if (phase_==Phase::idle && due && window && cycle!=cycle_) {
        cycle_=cycle; home_=view.server.configured_target;
        epoch_=std::to_string(session_)+"/"+std::to_string(cycle.count());
        phase_=Phase::outbound; deadline_=wall+std::chrono::minutes(10);
    }
    if (phase_==Phase::outbound) {
        const auto key=game::server_request_key(owner_,"mondo-public",epoch_,game::JoinTarget{}.uri());
        if (view.server.joined_request==key) phase_=Phase::ready;
        else if (wall>=deadline_) phase_=Phase::home;
    }
    if (phase_==Phase::ready && wall>=cycle_+std::chrono::minutes(15)) phase_=Phase::home;
    if (phase_==Phase::home && view.server.joined_request==game::server_request_key(owner_,"mondo-home",epoch_,home_)) phase_=Phase::idle;
}
bool MondoTrip::allows(const std::string& id) const {
    if (!enabled_) return true;
    const auto phase=phase_.load();
    return phase==Phase::ready || phase==Phase::fighting?id=="Mondo Chick":phase==Phase::idle && id!="Mondo Chick";
}
game::RequestBatch MondoTrip::requests() const {
    game::RequestBatch result{generation_,std::uint64_t(now_.count()),{}};
    const auto phase=phase_.load();
    if (phase==Phase::idle) return result;
    const bool home=phase==Phase::home;
    result.channels[extensions::RequestService::servers].push_back({home?"mondo-home":"mondo-public",95,now_+Time(3000),
        {{"target",home?home_:game::JoinTarget{}.uri()},{"epoch",epoch_},{"interrupt",home || interrupt_}}});
    return result;
}
namespace {
class TripProgram final : public automation::Program {
    std::shared_ptr<MondoTrip> trip_;
    MondoTrip::Phase phase_;
    automation::ProgramFactory fight_;
    bool launched_{};
    std::string outcome_;
public:
    TripProgram(std::shared_ptr<MondoTrip> trip,MondoTrip::Phase phase,automation::ProgramFactory fight)
        :trip_(std::move(trip)),phase_(phase),fight_(std::move(fight)) {}
    automation::RunConfig config() const override { return {std::chrono::hours(8),28,{},false,true}; }
    bool resume_after_reconnect(const automation::View&) override { return true; }
    ActivityStatus activity_status() const override { return {phase_==MondoTrip::Phase::fighting?"Fighting":"Rejoining",phase_==MondoTrip::Phase::home?"Private server":"Mondo Chick"}; }
    automation::Step next(const automation::View&) override {
        using namespace automation;
        if (phase_==MondoTrip::Phase::fighting) {
            if (!launched_) { launched_=true; return RunProgram{fight_,true}; }
            return Done{outcome_.empty()?"Mondo visit complete":outcome_};
        }
        if (trip_->phase()!=phase_) return Done{"Server request changed"};
        return Wait{Time(100),"Joining the requested server"};
    }
    void step_completed(const automation::View& view) override {
        if (view.completed && view.completed->state!=automation::State::completed) outcome_="Mondo visit ended: "+view.completed->message;
    }
    void finished(automation::State) override { if (phase_==MondoTrip::Phase::fighting) trip_->finished(); }
};
class Availability final : public automation::AvailabilitySource {
    std::shared_ptr<const automation::AvailabilitySource> source_;
    std::shared_ptr<MondoTrip> trip_;
public:
    Availability(std::shared_ptr<const automation::AvailabilitySource> source,std::shared_ptr<MondoTrip> trip):source_(std::move(source)),trip_(std::move(trip)) {}
    Observation<bool> read(const std::string& id,const automation::View& view) const override {
        return trip_->allows(id)?source_->read(id,view):Observation<bool>{Knowledge::present,false,view.context.now,view.context.window,view.context.generation};
    }
    Observation<Time> remaining(const std::string& id,const automation::View& view) const override {
        return trip_->allows(id)?source_->remaining(id,view):Observation<Time>{};
    }
};
}
std::unique_ptr<automation::Program> MondoTrip::program(automation::ProgramFactory fight) {
    if (phase_==Phase::ready) { phase_=Phase::fighting; return std::make_unique<TripProgram>(shared_from_this(),Phase::fighting,std::move(fight)); }
    return std::make_unique<TripProgram>(shared_from_this(),phase_.load(),automation::ProgramFactory{});
}
std::shared_ptr<const automation::AvailabilitySource> server_availability(std::shared_ptr<const automation::AvailabilitySource> source,std::shared_ptr<MondoTrip> trip) {
    return std::make_shared<Availability>(std::move(source),std::move(trip));
}
}
