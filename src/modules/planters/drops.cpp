#include "drops.hpp"
#include "materials.hpp"
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
        {"moon-charm","Moon Charm","Bee items"},{"coconut","Coconut","Consumables"},
        {"stickers","Stickers","Stickers"},{"dapper-sticker","Dapper From Above","Stickers"}};
    return goals;
}
std::span<const PlanterDrops> planter_drops() {
    static const PlanterDrops drops[]{
        {"PlasticPlanter","Plastic_Planter",{}, {},
            {"ticket","sunflower-seed","pineapple","blueberry","strawberry","bitterberry","neonberry","enzymes","gumdrops","royal-jelly","oil","glue","blue-extract","red-extract","soft-wax","swirled-wax","caustic-wax","super-smoothie"},
            "Wax bonuses: Coconut / Rose. Other rewards are random; no fixed base reward."},
        {"CandyPlanter","Candy_Planter",{{"gumdrops",5},{"jelly-beans",1},{"ticket",1}}, {"gumdrops","jelly-beans"},
            {"glue","oil","blue-extract","red-extract","enzymes","glitter","star-jelly","purple-potion","super-smoothie","caustic-wax","swirled-wax","turpentine"},
            "Extra Gumdrops in Blue Flower, Strawberry and Pineapple. Glue chain: Stump ×3. Pink Cupcake sticker: rare in Strawberry. Rare beequips and Gold Eggs are also reported."},
        {"BlueClayPlanter","Blue_Clay_Planter",{{"blue-extract",1},{"micro-converter",1},{"honeysuckle",3},{"ticket",1}}, {"blue-extract","micro-converter","honeysuckle"},
            {"pineapple","sunflower-seed","gumdrops","coconut","jelly-beans","smooth-dice","blueberry","bitterberry","neonberry","whirligig","royal-jelly","oil","glue","red-extract","swirled-wax","caustic-wax","turpentine"},
            "Spider's extra Micro-Converters have an unknown cooldown; only the base 1 is counted. Honeysuckles favor Sunflower, Blue Flower and Rose."},
        {"RedClayPlanter","Red_Clay_Planter",{}, {"red-extract","strawberry","soft-wax","hard-wax"},
            {"blueberry","blue-extract","sunflower-seed","oil","honeysuckle","glue","gumdrops","whirligig","stinger","field-dice","smooth-dice","swirled-wax","caustic-wax","ticket"},
            "Red Extract and Strawberries are reported at full growth; exact base amounts are not assumed. Stinger chain: Clover → Spider → Cactus. Pumpkin gives at least 2 Soft Wax."},
        {"TackyPlanter","Tacky_Planter",{{"field-dice",1},{"ticket",1}}, {"field-dice","smooth-dice","loaded-dice","sunflower-seed"},
            {"royal-jelly","strawberry","blueberry","pineapple","bitterberry","blue-extract","red-extract","coconut","moon-charm","soft-wax","hard-wax","enzymes","micro-converter","gumdrops","stinger","honeysuckle","whirligig","glitter","oil","jelly-beans","purple-potion","star-jelly","turpentine","super-smoothie","swirled-wax","caustic-wax"},
            "Mushroom → Blue Flower can give a Purple Potion; its cooldown is unknown. Dapper From Above is a one-time chain. Colorful Buttons, rare beequips and Gold Eggs are also reported."},
        {"PesticidePlanter","Pesticide_Planter",{{"neonberry",1},{"ticket",2}}, {"bitterberry","neonberry","caustic-wax"},
            {"stinger","blueberry","strawberry","sunflower-seed","pineapple","honeysuckle","royal-jelly","gumdrops","enzymes","moon-charm","smooth-dice","micro-converter","jelly-beans","soft-wax","hard-wax","field-dice","red-extract","blue-extract","glitter","glue","whirligig","coconut","tropical-drink","star-jelly","super-smoothie","purple-potion","swirled-wax","turpentine"},
            "Neonberry chain: Stump → Mountain Top → Coconut. Bitterberries favor Spider, Pepper and Mountain Top. Caustic Wax favors Bamboo and Strawberry. Rare Gold Eggs are also reported."},
        {"HeatTreatedPlanter","Heat-Treated_Planter",{{"red-extract",1},{"enzymes",1},{"ticket",3}}, {"red-extract","enzymes"},
            {"strawberry","sunflower-seed","bitterberry","neonberry","coconut","royal-jelly","gumdrops","honeysuckle","whirligig","field-dice","jelly-beans","hard-wax","loaded-dice","blue-extract","star-jelly","purple-potion","super-smoothie","swirled-wax","caustic-wax","turpentine","stinger"},
            "Red Extract chain: Mushroom → Strawberry → Rose → Pepper. Enzymes: Pineapple ×3. Cactus's 10 Stingers have an unknown cooldown. Dark Flame / Jack-O-Lantern stickers favor Pine Tree / Pumpkin. Rare beequips are also reported."},
        {"HydroponicPlanter","Hydroponic_Planter",{{"blue-extract",1},{"oil",1},{"ticket",3}}, {"blue-extract","oil"},
            {"blueberry","strawberry","sunflower-seed","pineapple","bitterberry","neonberry","gumdrops","royal-jelly","honeysuckle","whirligig","jelly-beans","loaded-dice","swirled-wax","caustic-wax","turpentine"},
            "Blue Extract chain: Blue Flower → Bamboo → Pine Tree → Stump. Oil: Sunflower ×3. Sitting Mother Bear sticker: extremely rare in Sunflower. Candy Ring beequip is also reported."},
        {"PetalPlanter","Petal_Planter",{{"glitter",1},{"ticket",5}}, {"glitter","jelly-beans","field-dice","smooth-dice","loaded-dice","star-jelly"},
            {"sunflower-seed","pineapple","blueberry","strawberry","oil","honeysuckle","whirligig","gumdrops","soft-wax","hard-wax","royal-jelly","moon-charm","glue","swirled-wax","caustic-wax","turpentine","super-smoothie"},
            "White-field Star Jelly and Glitter bonuses have inconsistent cooldown reports; not counted as repeating bonuses. Pale Heart sticker: very rare in Strawberry."},
        {"PlanterOfPlenty","The_Planter_Of_Plenty",{{"neonberry",5},{"ticket",10}}, {"neonberry","soft-wax","hard-wax","swirled-wax","caustic-wax","turpentine","micro-converter","star-jelly"},
            {"bitterberry","blueberry","strawberry","sunflower-seed","pineapple","coconut","gumdrops","enzymes","oil","red-extract","blue-extract","super-smoothie","stinger"},
            "Bamboo gives a monthly Super Smoothie bonus. Cactus's Stinger bonus has an unknown cooldown. Turpentine and wax quantities are random."},
        {"PaperPlanter","Paper_Planter",{}, {},
            {"ticket","sunflower-seed","strawberry","pineapple","blueberry","gumdrops","royal-jelly","whirligig","glue","enzymes","oil","red-extract","blue-extract","soft-wax","honeysuckle","stinger","field-dice","coconut","hard-wax","star-jelly","purple-potion","super-smoothie","caustic-wax","turpentine"},
            "Consumed on use. Sunflower Oil and Pineapple Enzymes have unknown cooldowns. Higher-tier rewards can be exceptionally rare."},
        {"TicketPlanter","Ticket_Planter",{{"ticket",10}}, {"ticket"},{},"Consumed on use. Extra Tickets are random; up to 150 are reported."},
        {"StickerPlanter","Sticker_Planter",{{"ticket",3},{"smooth-dice",1},{"neonberry",1},{"soft-wax",1},{"whirligig",1},{"stickers",5}}, {"stickers"},{},
            "Consumed on use. Sticker identities are random; no particular sticker is guaranteed."},
        {"FestivePlanter","Festive_Planter",{{"ticket",15}}, {},
            {"field-dice","smooth-dice","loaded-dice","soft-wax","hard-wax","whirligig","jelly-beans"},
            "Consumed on use. Also has seasonal rewards, eggs and Night Bells. This is not an exhaustive loot table."}};
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
std::string drop_description(const PlanterDrops& drops) {
    std::string text;
    const auto append=[&](const std::string& heading,const auto& items) {
        if(items.empty()) return;
        if(!text.empty()) text+='\n';text+=heading;
        bool first=true;for(const auto& item:items) {if(!first)text+=", ";text+=item;first=false;}
    };
    std::vector<std::string> fixed,favored,other;
    for(const auto& [id,amount]:drops.guaranteed) fixed.push_back(std::to_string(amount)+" "+item_label(id));
    for(const auto& id:drops.favored) favored.push_back(item_label(id));
    for(const auto& id:drops.other) other.push_back(item_label(id));
    append("At full growth: ",fixed);append("Favored drops: ",favored);append("Other reported drops: ",other);
    for(const auto& route:material_routes()) if(route.planter==drops.planter) {
        text+="\n"+std::to_string(route.amount)+" "+route.item+": ";
        for(std::size_t i=0;i<route.fields.size();++i) {if(i)text+=" → ";text+=route.fields[i];}
        text+=route.once?" · once per account":" · estimated "+std::to_string(std::chrono::duration_cast<std::chrono::days>(route.cooldown).count())+" days";
    }
    text+='\n';text+=drops.notes;
    text+="\nWiki: https://bee-swarm-simulator.fandom.com/wiki/"+drops.wiki;
    return text;
}
}
