#pragma once
#include "servers.hpp"
#include "modules/combat/plan.hpp"
#include "core/module_host/module.hpp"
#include <mutex>

namespace hive::features::vichop {
struct Services {
    combat::Enemy enemy;
    double speed{28};
    automation::ProgramFactory prepare,home;
    movement::Source movement;
    std::shared_ptr<const game::DetectionSource> daylight;
    std::shared_ptr<ServerDirectory> servers;
    automation::ProgramFactory arrival,joined_setup;
    std::function<void(const ObservationContext&)> confirmed;
};
struct Snapshot {
    bool active{},joined{};
    unsigned servers{},nights{},kills{},misses{},skipped{},failures{};
    std::string status{"Ready"},last_result;
    Time elapsed{};
    std::uint64_t epoch{};
};
class Controller {
    mutable std::mutex mutex_;
    std::string owner_,target_,epoch_,key_;
    std::uint64_t run_{},hop_{},generation_{};
    Time now_{},started_{};
    Snapshot state_;
public:
    explicit Controller(std::string owner):owner_(std::move(owner)) {}
    void begin();
    void stop();
    void release_server();
    void request(const game::JoinTarget& target);
    void update(const extensions::SessionView& view);
    void status(std::string text);
    void night();
    enum class Result { kill,miss,skipped,failed };
    void result(Result outcome,std::string text);
    Snapshot read() const;
    game::RequestBatch requests() const;
};
// One complete server visit; the module owns the session and counters between visits.
std::unique_ptr<automation::Program> visit(Services services,std::shared_ptr<Controller> controller,Time duration);
}
