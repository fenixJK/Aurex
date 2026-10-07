#pragma once
#include "hive/scripting/movement.hpp"
#include "program.hpp"
#include "hive/game/detection/respawn.hpp"
#include "hive/game/detection/hive_slots.hpp"
#include <mutex>

namespace hive::automation {
class HiveAlignmentError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
struct HiveSetupSnapshot {
    std::optional<unsigned> claimed_slot;
    bool camera_prepared{};
    ObservationContext context;
};
class HiveSetupResult {
    mutable std::mutex mutex_;
    std::optional<HiveSetupSnapshot> value_;
public:
    std::optional<HiveSetupSnapshot> read() const;
    void reset();
    void invalidate_camera();
    void begin_preparation(const ObservationContext& context);
    void claimed(unsigned slot,const ObservationContext& context);
    void prepared(const ObservationContext& context);
};
struct HiveClaimOptions {
    double speed{28}; unsigned preferred_slot{}; bool search_opposite_view{true};
    std::shared_ptr<HiveSetupResult> result;
    bool confirmed_spawn{};
    std::shared_ptr<const game::RespawnSource> recovery;
    unsigned existing_slot{};
    movement::Source movement;
};
ApproachPrompt hive_row_approach(double speed,movement::Source movement={});
std::unique_ptr<Program> acquire_hive(std::shared_ptr<const game::HiveSlotsSource> slots,
    std::shared_ptr<const game::LandmarkSource> spawn,std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,HiveClaimOptions options={});
std::unique_ptr<Program> respawn_to_hive(std::shared_ptr<const game::HiveSlotsSource> slots,
    std::shared_ptr<const game::LandmarkSource> spawn,std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,HiveClaimOptions options);
std::unique_ptr<Program> recover_to_hive(std::shared_ptr<const game::HiveSlotsSource> slots,
    std::shared_ptr<const game::LandmarkSource> spawn,std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,HiveClaimOptions options);
enum class ReconnectStart { request,loading };
std::unique_ptr<Program> reconnect_to_hive(std::shared_ptr<const game::HiveSlotsSource> slots,
    std::shared_ptr<const game::LandmarkSource> spawn,std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,HiveClaimOptions options,ReconnectStart start=ReconnectStart::request);
std::unique_ptr<Program> claim_hive_from_spawn(std::shared_ptr<const game::HiveSlotsSource> source, HiveClaimOptions options={});
std::unique_ptr<Program> survey_hive_slots(std::shared_ptr<const game::HiveSlotsSource> source);
ApproachLandmark hive_reference_search(std::shared_ptr<const game::LandmarkSource> reference,movement::Source movement={});
std::unique_ptr<Program> find_hive_reference(std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,movement::Source movement={});
std::unique_ptr<Program> prepare_hive_view(std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,std::shared_ptr<HiveSetupResult> result={});
}
