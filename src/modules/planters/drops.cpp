#include "drops.hpp"
#include <algorithm>

namespace hive::features::planters {
std::span<const ItemGoal> item_goals() {
    static const ItemGoal goals[]{
        {"glue","Glue","Materials"},{"oil","Oil","Materials"},{"enzymes","Enzymes","Materials"},
        {"blue-extract","Blue Extract","Materials"},{"red-extract","Red Extract","Materials"},
        {"glitter","Glitter","Materials"},{"tropical-drink","Tropical Drink","Materials"},
        {"purple-potion","Purple Potion","Materials"},{"super-smoothie","Super Smoothie","Materials"},
        {"soft-wax","Soft Wax","Waxes"},{"hard-wax","Hard Wax","Waxes"},
        {"swirled-wax","Swirled Wax","Waxes"},{"caustic-wax","Caustic Wax","Waxes"},{"turpentine","Turpentine","Waxes"},
        {"gumdrops","Gumdrops","Consumables"},{"jelly-beans","Jelly Beans","Consumables"},
        {"micro-converter","Micro-Converter","Consumables"},{"honeysuckle","Honeysuckle","Consumables"},
        {"whirligig","Whirligig","Consumables"},{"stinger","Stinger","Consumables"},{"ticket","Ticket","Consumables"},
        {"field-dice","Field Dice","Consumables"},{"smooth-dice","Smooth Dice","Consumables"},{"loaded-dice","Loaded Dice","Consumables"},
        {"royal-jelly","Royal Jelly","Bee items"},{"star-jelly","Star Jelly","Bee items"},
        {"sunflower-seed","Sunflower Seed","Bee items"},{"blueberry","Blueberry","Bee items"},
        {"strawberry","Strawberry","Bee items"},{"pineapple","Pineapple","Bee items"},
        {"bitterberry","Bitterberry","Bee items"},{"neonberry","Neonberry","Bee items"},
        {"moon-charm","Moon Charm","Bee items"},{"coconut","Coconut","Consumables"}};
    return goals;
}
std::span<const PlanterDrops> planter_drops() {
    static const PlanterDrops drops[]{
        {"PlasticPlanter",{}, {},
            {"ticket","sunflower-seed","pineapple","blueberry","strawberry","bitterberry","neonberry","enzymes","gumdrops","royal-jelly","oil","glue","blue-extract","red-extract","soft-wax","swirled-wax","caustic-wax","super-smoothie"}},
        {"CandyPlanter",{{"gumdrops",5},{"jelly-beans",1},{"ticket",1}}, {"gumdrops","jelly-beans"},
            {"glue","oil","blue-extract","red-extract","enzymes","glitter","star-jelly","purple-potion","super-smoothie","caustic-wax","swirled-wax","turpentine"}},
        {"BlueClayPlanter",{{"blue-extract",1},{"micro-converter",1},{"honeysuckle",3},{"ticket",1}}, {"blue-extract","micro-converter","honeysuckle"},
            {"pineapple","sunflower-seed","gumdrops","coconut","jelly-beans","smooth-dice","blueberry","bitterberry","neonberry","whirligig","royal-jelly","oil","glue","red-extract","swirled-wax","caustic-wax","turpentine"}},
        {"RedClayPlanter",{}, {"red-extract","strawberry","soft-wax","hard-wax"},
            {"blueberry","blue-extract","sunflower-seed","oil","honeysuckle","glue","gumdrops","whirligig","stinger","field-dice","smooth-dice","swirled-wax","caustic-wax","ticket"}},
        {"TackyPlanter",{{"field-dice",1},{"ticket",1}}, {"field-dice","smooth-dice","loaded-dice","sunflower-seed"},
            {"royal-jelly","strawberry","blueberry","pineapple","bitterberry","blue-extract","red-extract","coconut","moon-charm","soft-wax","hard-wax","enzymes","micro-converter","gumdrops","stinger","honeysuckle","whirligig","glitter","oil","jelly-beans","purple-potion","star-jelly","turpentine","super-smoothie","swirled-wax","caustic-wax"}},
        {"PesticidePlanter",{{"neonberry",1},{"ticket",2}}, {"bitterberry","neonberry","caustic-wax"},
            {"stinger","blueberry","strawberry","sunflower-seed","pineapple","honeysuckle","royal-jelly","gumdrops","enzymes","moon-charm","smooth-dice","micro-converter","jelly-beans","soft-wax","hard-wax","field-dice","red-extract","blue-extract","glitter","glue","whirligig","coconut","tropical-drink","star-jelly","super-smoothie","purple-potion","swirled-wax","turpentine"}},
        {"HeatTreatedPlanter",{{"red-extract",1},{"enzymes",1},{"ticket",3}}, {"red-extract","enzymes"},
            {"strawberry","sunflower-seed","bitterberry","neonberry","coconut","royal-jelly","gumdrops","honeysuckle","whirligig","field-dice","jelly-beans","hard-wax","loaded-dice","blue-extract","star-jelly","purple-potion","super-smoothie","swirled-wax","caustic-wax","turpentine","stinger"}},
        {"HydroponicPlanter",{{"blue-extract",1},{"oil",1},{"ticket",3}}, {"blue-extract","oil"},
            {"blueberry","strawberry","sunflower-seed","pineapple","bitterberry","neonberry","gumdrops","royal-jelly","honeysuckle","whirligig","jelly-beans","loaded-dice","swirled-wax","caustic-wax","turpentine"}},
        {"PetalPlanter",{{"glitter",1},{"ticket",5}}, {"glitter","jelly-beans","field-dice","smooth-dice","loaded-dice","star-jelly"},
            {"sunflower-seed","pineapple","blueberry","strawberry","oil","honeysuckle","whirligig","gumdrops","soft-wax","hard-wax","royal-jelly","moon-charm","glue","swirled-wax","caustic-wax","turpentine","super-smoothie"}},
        {"PlanterOfPlenty",{{"neonberry",5},{"ticket",10}}, {"neonberry","soft-wax","hard-wax","swirled-wax","caustic-wax","turpentine","micro-converter","star-jelly"},
            {"bitterberry","blueberry","strawberry","sunflower-seed","pineapple","coconut","gumdrops","enzymes","oil","red-extract","blue-extract","super-smoothie","stinger"}},
        {"PaperPlanter",{}, {},
            {"ticket","sunflower-seed","strawberry","pineapple","blueberry","gumdrops","royal-jelly","whirligig","glue","enzymes","oil","red-extract","blue-extract","soft-wax","honeysuckle","stinger","field-dice","coconut","hard-wax","star-jelly","purple-potion","super-smoothie","caustic-wax","turpentine"}},
        {"TicketPlanter",{{"ticket",10}}, {"ticket"},{}},
        {"StickerPlanter",{{"ticket",3},{"smooth-dice",1},{"neonberry",1},{"soft-wax",1},{"whirligig",1}}, {},{}},
        {"FestivePlanter",{{"ticket",15}}, {},
            {"field-dice","smooth-dice","loaded-dice","soft-wax","hard-wax","whirligig","jelly-beans"}}};
    return drops;
}
std::string item_label(const std::string& goal) {
    for (const auto& item:item_goals()) if(item.id==goal) return item.label;
    return goal;
}
ItemSource item_source(const std::string& goal,const Stats& stats) {
    ItemSource source;
    const auto in=[&](const auto& values) {return std::find(values.begin(),values.end(),goal)!=values.end();};
    for(const auto& drops:planter_drops()) if(drops.planter==stats.planter) {
        if(const auto fixed=drops.guaranteed.find(goal);fixed!=drops.guaranteed.end()) {source.minimum=fixed->second;source.preference=3;}
        else if(in(drops.favored)) source.preference=2;
        else if(in(drops.other)) source.preference=1;
    }
    struct FieldDrop { const char* goal; std::vector<std::string> fields; };
    static const FieldDrop field_drops[]{
        {"sunflower-seed",{"Sunflower"}},{"oil",{"Sunflower"}},{"pineapple",{"Pineapple"}},
        {"enzymes",{"Pineapple"}},{"moon-charm",{"Pineapple"}},
        {"blueberry",{"Blue Flower","Bamboo","Pine Tree","Stump"}},{"blue-extract",{"Blue Flower","Bamboo","Pine Tree","Stump"}},
        {"strawberry",{"Mushroom","Strawberry","Rose","Pepper"}},{"red-extract",{"Mushroom","Strawberry","Rose","Pepper"}},
        {"bitterberry",{"Spider","Pepper"}},{"gumdrops",{"Stump"}},{"glue",{"Stump"}},
        {"stinger",{"Cactus","Rose"}},{"whirligig",{"Pine Tree"}},{"coconut",{"Coconut"}},
        {"tropical-drink",{"Coconut"}},{"soft-wax",{"Mushroom","Pumpkin"}},{"hard-wax",{"Pumpkin"}}};
    for(const auto& drop:field_drops) if(goal==drop.goal && std::find(drop.fields.begin(),drop.fields.end(),stats.field)!=drop.fields.end()) {
        source.field_bonus=true;source.preference=std::max(1,source.preference);
    }
    if((goal=="coconut" || goal=="tropical-drink") && stats.field!="Coconut") return {};
    if(stats.planter=="CandyPlanter" && goal=="gumdrops") source.field_bonus=stats.field=="Blue Flower" || stats.field=="Strawberry" || stats.field=="Pineapple";
    if(stats.planter=="BlueClayPlanter" && goal=="micro-converter") source.field_bonus=stats.field=="Spider";
    if(stats.planter=="BlueClayPlanter" && goal=="honeysuckle") source.field_bonus=stats.field=="Sunflower" || stats.field=="Blue Flower" || stats.field=="Rose";
    if(stats.planter=="RedClayPlanter" && goal=="soft-wax" && stats.field=="Pumpkin") {source.minimum=2;source.preference=3;}
    if(stats.planter=="PesticidePlanter" && goal=="caustic-wax") source.field_bonus=stats.field=="Bamboo" || stats.field=="Strawberry";
    if(stats.planter=="PesticidePlanter" && goal=="bitterberry" && stats.field=="Mountain Top") source.field_bonus=true;
    return source;
}
}
