#include "plan.hpp"
#include "modules/shared/movement.hpp"
#include "game/actions/travel/field_routes.hpp"
#include "game/detection/daylight.hpp"

namespace hive::features::combat {
int commando_health(int level) {
    static constexpr int health[]{150,2000,10000,15000,25000,50000,100000,150000,200000,300000,400000,500000,750000,1000000,2500000,5000000,7500000,10000000};
    if (level<3 || level>25) throw std::invalid_argument("Commando level must be between 3 and 25");
    return health[std::min(level-3,17)];
}
Plan walking_plan(const workflows::RouteCatalog& routes,double speed,std::shared_ptr<const game::DetectionSource> observations,
    automation::ProgramFactory return_to_hive,automation::FieldReturnOptions returns,std::shared_ptr<const game::DetectionSource> daylight) {
    if (!observations || !std::isfinite(speed) || speed<8 || speed>200)
        throw std::invalid_argument("Walking combat needs enemy detection and a valid movement speed");
    struct Recipe {
        const char* id; const char* species; const char* field; Time cooldown; bool selected_by_default;
        const char* group{};
        unsigned count{1};
        Time fight_time{120000};
        bool boss{};
    };
    const Recipe recipes[]{
        {"Ladybug (Mushroom)","Ladybug","mushroom",Time(330000),true},
        {"Spider","Spider","spider",Time(1830000),false},
        {"Rhino Beetle (Blue Flower)","Rhino Beetle","blueflower",Time(330000),false},
        {"Ladybug (Clover)","Ladybug","clover",Time(330000),false,"clover"},
        {"Rhino Beetle (Clover)","Rhino Beetle","clover",Time(330000),false,"clover"},
        {"Ladybugs (Strawberry)","Ladybug","strawberry",Time(330000),false,nullptr,2},
        {"Rhino Beetles (Bamboo)","Rhino Beetle","bamboo",Time(330000),false,nullptr,2},
        {"Rhino Beetle (Pineapple)","Rhino Beetle","pineapple",Time(330000),false,"pineapple"},
        {"Mantis (Pineapple)","Mantis","pineapple",Time(1230000),false,"pineapple"},
        {"Mantises (Pine Tree)","Mantis","pinetree",Time(1230000),false,nullptr,2},
        {"Scorpions (Rose)","Scorpion","rose",Time(1230000),false,nullptr,2},
        {"Werewolf","Werewolf","pumpkin",Time(3630000),false},
        {"King Beetle","King Beetle","boss.king-beetle",Time(86400000),false,nullptr,1,Time(600000),true},
        {"Tunnel Bear","Tunnel Bear","boss.tunnel-bear",Time(172800000),false,nullptr,1,Time(180000),true},
        {"Commando Chick","Commando Chick","boss.commando",Time(1800000),false,nullptr,1,Time(1200000),true},
        {"Mondo Chick","Mondo Chick","mountaintop",Time(2700000),false,nullptr,1,Time(600000),true},
        {"Coconut Crab","Coconut Crab","coconut",Time(129600000),false,nullptr,1,Time(1200000),true},
        {"Stump Snail","Stump Snail","stump",Time(345600000),false,nullptr,1,Time(21600000),true}};
    Plan plan; plan.movement=returns.movement;
    const auto motions=movement::snapshot(returns.movement);
    plan.finish_at="hive.proximity"; plan.config={std::chrono::hours(8),speed,{},true,true};
    for (const auto& recipe : recipes) {
        Enemy enemy; enemy.id=recipe.id; enemy.species=recipe.species; enemy.destination=std::string(recipe.field).starts_with("boss.")?recipe.field:std::string(recipe.field)+".entry";
        if (!routes.network().contains(enemy.destination) || !routes.network().contains(*plan.finish_at)) continue;
        bool outbound=false,inbound=false;
        for (const auto& route : routes.network().routes()) {
            if (route.from==*plan.finish_at && route.to==enemy.destination) outbound=true;
            if (route.from==enemy.destination && route.to==*plan.finish_at) inbound=true;
        }
        if (!outbound || (!inbound && !return_to_hive)) continue;
        enemy.approach_location=enemy.destination;
        enemy.cooldown=recipe.cooldown; enemy.inspect_when_unknown=true; enemy.selected_by_default=recipe.selected_by_default;
        enemy.count=recipe.count; enemy.observations=observations; enemy.boss=recipe.boss;
        if (enemy.species=="Mondo Chick") {
            enemy.maximum_health=300000; enemy.shared=true; enemy.field="mountaintop";
            enemy.respawn_reduction=false; enemy.avoid={"Vicious Bee"};
            enemy.scheduled=[](const ObservationContext&) { return std::chrono::duration_cast<std::chrono::minutes>(std::chrono::system_clock::now().time_since_epoch()).count()%60<15; };
            enemy.revisit=[] {
                const auto now=std::chrono::duration_cast<Time>(std::chrono::system_clock::now().time_since_epoch());
                return std::chrono::duration_cast<Time>(std::chrono::hours(1))-now%std::chrono::duration_cast<Time>(std::chrono::hours(1));
            };
            plan.options.enemies[enemy.id].boss=MondoOptions{};
        }
        if (enemy.species=="King Beetle") { enemy.maximum_health=2500; enemy.amulet="King Beetle Amulet"; enemy.amulet_optional=true; plan.options.enemies[enemy.id].boss=BeetleOptions{}; }
        if (enemy.species=="Tunnel Bear") { enemy.maximum_health=10000; enemy.movement_radius=30; enemy.movement_interval=Time(700); }
        if (enemy.species=="Coconut Crab") enemy.maximum_health=250000;
        if (enemy.species=="Commando Chick") { enemy.maximum_health=commando_health(3); enemy.stationary=true; plan.options.enemies[enemy.id].boss=CommandoOptions{}; }
        if (recipe.group) enemy.group=recipe.group;
        enemy.fight={movement_walks(*motions,"combat",{{"enemy",enemy.species},{"loot",false}},true),recipe.fight_time,{},{},"Fighting "+enemy.species,
            game::enemy_defeat_source(observations,enemy.species)};
        if (enemy.species=="Stump Snail") {
            enemy.stationary=true; enemy.amulet="Shell Amulet"; enemy.maximum_health=30000000;
            plan.options.enemies[enemy.id]={true,Time(900000),SnailOptions{}};
        }
        if (enemy.maximum_health) enemy.progress=std::make_shared<FightProgress>();
        enemy.loot=movement_walks(*motions,"combat",{{"enemy",enemy.species},{"loot",true}});
        if (!inbound) enemy.departure=return_to_hive;
        else enemy.departure=[field=std::string(recipe.field),speed,returns] { return automation::return_from_field(field,speed,0,returns); };
        plan.enemies.push_back(std::move(enemy));
    }
    if (return_to_hive && daylight) {
        Enemy enemy; enemy.id=enemy.species="Vicious Bee"; enemy.shared=true; enemy.destination="hive.proximity";
        enemy.boss=true; enemy.selected_by_default=false; enemy.inspect_when_unknown=true;
        enemy.cooldown=std::chrono::minutes(20); enemy.respawn_reduction=false;
        enemy.scheduled=game::during_night(std::move(daylight)); enemy.observations=observations; enemy.departure=return_to_hive;
        enemy.progress=std::make_shared<FightProgress>();
        enemy.fight={movement_walks(*motions,"combat",{{"enemy",enemy.species},{"loot",false}},true),Time(300000),{},{},"Fighting "+enemy.species,
            game::enemy_defeat_source(observations,enemy.species)};
        for (const auto* field:{"pepper","mountaintop","rose","cactus","spider","clover"})
            if (routes.network().contains(std::string(field)+".entry")) enemy.hunt_fields.push_back(field);
        if (!enemy.hunt_fields.empty()) plan.enemies.push_back(std::move(enemy));
    }
    return plan;
}
}
