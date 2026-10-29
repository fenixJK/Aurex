#pragma once
#include "game/actions/activities/visits.hpp"
#include "game/actions/travel/field_routes.hpp"
#include "game/detection/enemies.hpp"
#include "progress.hpp"

namespace hive::workflows { class RouteCatalog; }

namespace hive::features::combat {
struct Enemy {
    std::string id,destination;
    automation::Pattern fight;
    std::vector<automation::Walk> loot;
    Time cooldown{};
    bool inspect_when_unknown{};
    automation::ProgramFactory departure;
    std::shared_ptr<const game::LandmarkSource> approach_scope;
    std::string approach_location;
    std::string species;
    bool selected_by_default{true};
    std::string group;
    unsigned count{1};
    std::shared_ptr<const game::DetectionSource> observations;
    std::function<bool(const ObservationContext&)> scheduled;
    bool respawn_reduction{true};
    bool stationary{};
    std::string amulet;
    std::vector<std::string> avoid;
    bool boss{};
    int maximum_health{};
    int initial_health{};
    Time session_limit{};
    std::function<Time()> revisit;
    bool keep_old{true};
    std::shared_ptr<FightProgress> progress;
    Time loot_duration{};
    bool amulet_optional{};
    double movement_radius{};
    Time movement_interval{};
    std::vector<automation::Walk> search;
    std::vector<std::string> hunt_fields;
    bool shared{};
    std::string field;
    std::function<std::string(const ObservationContext&)> requested_field;
    std::shared_ptr<const game::DetectionSource> vicious;
    std::shared_ptr<const game::DetectionSource> daylight;
    bool scout_route{};
    std::shared_ptr<automation::HiveSetupResult> scout_hive;
    std::shared_ptr<const game::LandmarkSource> scout_transport;
};
enum class MondoMode { kill,buff };
enum class LootDirection { random,left,right };
struct MondoOptions {
    MondoMode mode{MondoMode::kill};
    int seconds{120};
    LootDirection direction{LootDirection::random};
    int loot_seconds{45};
    int lead_seconds{};
};
struct SnailOptions {
    int health{30000000};
    bool until_defeated{},keep_old{true};
};
struct CommandoOptions { int level{3}; bool until_defeated{}; int health{}; };
struct BeetleOptions { bool keep_old{true}; };
struct HuntOptions { SettingSelection fields; bool daily_only{}; };
std::vector<std::string> select_hunt_fields(const std::vector<std::string>& available,const SettingSelection& selected);
int commando_health(int level);
std::unique_ptr<automation::Program> wait_for_buff(std::string location,std::string buff,std::shared_ptr<const game::DetectionSource> source,double speed,std::vector<workflows::CheckpointPlan> entry={});
std::unique_ptr<automation::Program> wait_for_spawn(std::string location,Time deadline,std::function<Time()> clock,double speed);
struct EnemyOptions {
    bool collect_loot{true};
    Time fight_limit{};
    std::variant<std::monostate,MondoOptions,SnailOptions,CommandoOptions,BeetleOptions,HuntOptions> boss;
    bool baby_love{};
};
struct Options {
    int respawn_reduction{};
    std::map<std::string,EnemyOptions> enemies;
    SettingSelection boss_order;
    bool reset_return{true};
};
struct Plan {
    std::vector<Enemy> enemies;
    std::optional<std::string> finish_at;
    automation::RunConfig config{Time(3600000),28,{},true,true};
    automation::ProgramFactory setup;
    Options options;
    movement::Source movement;
    automation::ProgramFactory recovery;
    std::shared_ptr<const game::DetectionSource> buffs;
    std::function<Time()> wall_clock=[] { return std::chrono::duration_cast<Time>(std::chrono::system_clock::now().time_since_epoch()); };
};
Plan catalog_plan();
Plan walking_plan(const workflows::RouteCatalog& routes,double speed,std::shared_ptr<const game::DetectionSource> observations,
    automation::ProgramFactory return_to_hive={},automation::FieldReturnOptions returns={},std::shared_ptr<const game::DetectionSource> daylight={});
automation::VisitPlan visit_plan(Plan plan);
enum class HuntStart { hive, spawn };
bool uses_scout_route(const Enemy& enemy);
std::unique_ptr<automation::Program> hunt(const Enemy& enemy,double speed,automation::ProgramFactory prepare,automation::ProgramFactory home,
    movement::Source movement,HuntStart start=HuntStart::hive);
std::unique_ptr<automation::Program> encounter(const Enemy& enemy,double speed,Time entered);
std::shared_ptr<const automation::AvailabilitySource> availability(std::map<std::string,std::string> enemies,
    std::shared_ptr<const game::EnemySource> source);
std::shared_ptr<const automation::AvailabilitySource> availability_for(const Plan& plan,std::shared_ptr<const game::EnemySource> source);
automation::VisitRun program(Plan plan,std::shared_ptr<const game::RouteNetwork> network,
    std::shared_ptr<const automation::AvailabilitySource> availability);
}
