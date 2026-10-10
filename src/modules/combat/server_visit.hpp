#pragma once
#include "core/module_host/module.hpp"
#include "game/actions/activities/visits.hpp"
#include <atomic>

namespace hive::features::combat {
class MondoTrip : public std::enable_shared_from_this<MondoTrip> {
public:
    enum class Phase { idle,outbound,ready,fighting,home };
private:
    std::string owner_,home_,epoch_;
    std::atomic<Phase> phase_{Phase::idle};
    std::atomic<bool> enabled_{};
    bool selected_{},interrupt_{};
    int lead_{};
    std::uint64_t session_{};
    Time cycle_{Time(-1)},now_{},deadline_{};
    std::uint64_t generation_{};
public:
    explicit MondoTrip(std::string owner):owner_(std::move(owner)) {}
    void configure(bool selected,int lead,bool interrupt);
    void update(const extensions::SessionView&,Time wall,bool due);
    bool active() const { return phase_!=Phase::idle; }
    bool selected() const { return selected_; }
    bool ready() const { return phase_==Phase::ready; }
    Phase phase() const { return phase_; }
    bool allows(const std::string&) const;
    game::RequestBatch requests() const;
    std::unique_ptr<automation::Program> program(automation::ProgramFactory fight);
    void finished() { phase_=Phase::home; }
};
std::shared_ptr<const automation::AvailabilitySource> server_availability(std::shared_ptr<const automation::AvailabilitySource>,std::shared_ptr<MondoTrip>);
}
