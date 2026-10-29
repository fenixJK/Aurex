#include "field_settings.hpp"
#include "core/runtime/model.hpp"
#include <array>
#include <cmath>
#include <stdexcept>

namespace hive::features::gathering {
std::string field_prefix(const std::string& field) {
    std::uint64_t hash=14695981039346656037ULL;
    for (unsigned char c:field) { hash^=c; hash*=1099511628211ULL; }
    return "field-"+std::to_string(hash);
}
Settings import_natro_field(const std::string& field,const Settings& copied) {
    const auto text=[&](const std::string& key) {
        const auto it=copied.find(key);
        if (it==copied.end() || !std::holds_alternative<std::string>(it->second)) throw std::invalid_argument("Missing or invalid Natro option: "+key);
        return std::get<std::string>(it->second);
    };
    const auto number=[&](const std::string& key,int lo,int hi) {
        const auto it=copied.find(key);
        const auto n=it==copied.end()?nullptr:std::get_if<int>(&it->second);
        if (!n || *n<lo || *n>hi) throw std::invalid_argument("Missing or invalid Natro option: "+key);
        return *n;
    };
    if (text("Name")!=field) throw std::invalid_argument("Select "+text("Name")+" before pasting these settings.");
    const auto prefix=field_prefix(field)+"-";
    auto pattern=text("Pattern"); if (pattern=="Lines") pattern="Natro Lines";
    const std::map<std::string,int> scales{{"XS",25},{"S",50},{"M",100},{"L",150},{"XL",200}};
    const auto scale=scales.find(text("PatternSize"));
    if (scale==scales.end()) throw std::invalid_argument("Unknown Natro pattern size.");
    const auto direction=text("RotateDirection");
    if (direction!="None" && direction!="Right" && direction!="Left") throw std::invalid_argument("Unknown Natro camera direction.");
    const int turns=number("RotateTimes",0,4);
    const unsigned heading=direction=="None"?0:direction=="Right"?turns:(8-turns)%8;
    const std::array<std::string,8> facings{"0°","45° right","90° right","135° right","180°","135° left","90° left","45° left"};
    // Natro's nm_setSprinkler uses milliseconds at 18 studs/sec; Aurex uses
    // four-stud tiles. Distance is a tenth of the field's documented extent.
    const std::map<std::string,Point> extents{{"Sunflower",{2000,1250}},{"Dandelion",{1000,2500}},
        {"Mushroom",{1750,1250}},{"Blue Flower",{750,2750}},{"Clover",{1500,2000}},
        {"Spider",{2000,2000}},{"Strawberry",{2000,1500}},{"Bamboo",{1250,3000}},
        {"Pineapple",{3000,1750}},{"Stump",{1500,1500}},{"Cactus",{2500,1500}},
        {"Pumpkin",{2500,1500}},{"Pine Tree",{1750,2500}},{"Rose",{1500,2500}},
        {"Mountain Top",{1500,2250}},{"Coconut",{2250,1500}},{"Pepper",{2250,1500}}};
    const auto size=extents.find(field);
    if (size==extents.end()) throw std::invalid_argument("No Natro position conversion for this field.");
    const std::map<std::string,Point> directions{{"Center",{0,0}},{"Upper Left",{-1,1}},
        {"Upper",{0,1}},{"Upper Right",{1,1}},{"Right",{1,0}},{"Lower Right",{1,-1}},
        {"Lower",{0,-1}},{"Lower Left",{-1,-1}},{"Left",{-1,0}}};
    const auto position=directions.find(text("SprinklerLoc"));
    if (position==directions.end()) throw std::invalid_argument("Unknown Natro sprinkler position.");
    const double distance=number("SprinklerDist",1,10)*.0045;
    const auto returning=text("ReturnType");
    if (returning!="Walk" && returning!="Reset") throw std::invalid_argument("This Natro return method cannot be imported; choose Walk or Reset.");
    return {{prefix+"pattern",pattern},{prefix+"pattern-scale",scale->second},
        {prefix+"pattern-repetitions",number("PatternReps",1,9)},
        {prefix+"invert-fb",bool(number("PatternInvertFB",0,1))},{prefix+"invert-lr",bool(number("PatternInvertLR",0,1))},
        {prefix+"shift-lock",bool(number("PatternShift",0,1))},{prefix+"recenter",bool(number("DriftCheck",0,1))},
        {prefix+"facing",facings[heading]},
        {prefix+"start-side",int(std::round(size->second.x*distance*position->second.x))},
        {prefix+"start-forward",int(std::round(size->second.y*distance*position->second.y))},
        {prefix+"gather-minutes",number("UntilMins",1,120)},{prefix+"backpack-percent",number("UntilPack",1,100)},
        {prefix+"return-method",returning},{prefix+"extend-while",SettingSelection{}}};
}
}
