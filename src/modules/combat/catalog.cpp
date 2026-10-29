#include "plan.hpp"
#include "modules/shared/movement.hpp"
#include "game/actions/travel/field_routes.hpp"
#include "game/detection/daylight.hpp"
#include <algorithm>
#include <cmath>

namespace hive::features::combat {
int commando_health(int level) {
    static constexpr int health[]{150,2000,10000,15000,25000,50000,100000,150000,200000,300000,400000,500000,750000,1000000,2500000,5000000,7500000,10000000};
    if (level<3 || level>25) throw std::invalid_argument("Commando level must be between 3 and 25");
    return health[std::min(level-3,17)];
}
Plan catalog_plan() {
    struct Recipe {
        const char* id; const char* species; const char* field; Time cooldown; bool selected_by_default;
        const char* group{};
        unsigned count{1};
        Time fight_time{120000};
        bool boss{};
    };
    const Recipe recipes[]{
        {"Ladybug (Mushroom)","Ladybug","mushroom",Time(330000),false},
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
    Plan plan; plan.config.duration=std::chrono::hours(8);
    for (const auto& recipe : recipes) {
        Enemy enemy; enemy.id=recipe.id; enemy.species=recipe.species; enemy.destination=std::string(recipe.field).starts_with("boss.")?recipe.field:std::string(recipe.field)+".entry";
        enemy.approach_location=enemy.destination;
        enemy.cooldown=recipe.cooldown; enemy.inspect_when_unknown=true; enemy.selected_by_default=recipe.selected_by_default;
        enemy.count=recipe.count; enemy.boss=recipe.boss;
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
        enemy.fight.duration=recipe.fight_time;
        if (enemy.species=="Stump Snail") {
            enemy.stationary=true; enemy.amulet="Shell Amulet"; enemy.maximum_health=30000000;
            plan.options.enemies[enemy.id]={true,Time(900000),SnailOptions{}};
        }
        plan.enemies.push_back(std::move(enemy));
    }
    Enemy enemy; enemy.id=enemy.species="Vicious Bee"; enemy.shared=true; enemy.destination="hive.proximity";
    enemy.boss=true; enemy.selected_by_default=false; enemy.inspect_when_unknown=true;
    enemy.cooldown=std::chrono::minutes(20); enemy.respawn_reduction=false;
    enemy.fight.duration=Time(300000);
    enemy.hunt_fields={"pepper","mountaintop","spider","cactus","rose","clover"};
    plan.enemies.push_back(std::move(enemy));
    return plan;
}
Plan walking_plan(const workflows::RouteCatalog& routes,double speed,std::shared_ptr<const game::DetectionSource> observations,
    automation::ProgramFactory return_to_hive,automation::FieldReturnOptions returns,std::shared_ptr<const game::DetectionSource> daylight) {
    if (!observations || !std::isfinite(speed) || speed<8 || speed>200)
        throw std::invalid_argument("Walking combat needs enemy detection and a valid movement speed");
    auto plan=catalog_plan(); plan.movement=returns.movement; plan.config.speed=speed; plan.finish_at="hive.proximity";
    const auto motions=movement::snapshot(returns.movement);
    std::erase_if(plan.enemies,[&](auto& enemy) {
        if (!enemy.hunt_fields.empty()) {
            if (!return_to_hive || !daylight) return true;
            std::erase_if(enemy.hunt_fields,[&](const auto& field) { return !routes.network().contains(field+".entry"); });
            if (enemy.hunt_fields.empty()) return true;
            enemy.daylight=daylight; enemy.scheduled=game::during_night(daylight); enemy.departure=return_to_hive;
            enemy.scout_hive=returns.hive;enemy.scout_transport=returns.transport_prompt;
            enemy.scout_route=returns.access.cannon && bool(returns.transport_prompt)
                && routes.network().contains("pepper.entry") && routes.network().contains("mountaintop.entry")
                && routes.network().contains("spider.entry") && routes.network().contains("cactus.entry")
                && routes.network().contains("rose.entry");
        } else {
            if (!routes.network().contains(enemy.destination) || !routes.network().contains(*plan.finish_at)) return true;
            bool outbound=false,inbound=false;
            for (const auto& route:routes.network().routes()) {
                if (route.from==*plan.finish_at && route.to==enemy.destination) outbound=true;
                if (route.from==enemy.destination && route.to==*plan.finish_at) inbound=true;
            }
            if (!outbound || (!inbound && !return_to_hive)) return true;
            if (!inbound) enemy.departure=return_to_hive;
            else {
                auto field=enemy.destination;
                if (field.ends_with(".entry")) field.resize(field.size()-6);
                enemy.departure=[field=std::move(field),speed,returns] { return automation::return_from_field(field,speed,0,returns); };
            }
            enemy.loot=movement_walks(*motions,"combat",{{"enemy",enemy.species},{"loot",true}});
        }
        enemy.observations=observations;
        enemy.fight={movement_walks(*motions,"combat",{{"enemy",enemy.species},{"loot",false}},true),enemy.fight.duration,{},{},"Fighting "+enemy.species,
            game::enemy_defeat_source(observations,enemy.species)};
        if (enemy.maximum_health || !enemy.hunt_fields.empty()) enemy.progress=std::make_shared<FightProgress>();
        return false;
    });
    std::erase_if(plan.options.enemies,[&](const auto& item) {
        return std::none_of(plan.enemies.begin(),plan.enemies.end(),[&](const auto& enemy) { return enemy.id==item.first; });
    });
    return plan;
}
}
