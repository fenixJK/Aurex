#include "buff_catalog.hpp"
#include "status_buff_icons.hpp"
#include "summary_buff_icons.hpp"
#include "buff_profile_icons.hpp"
#include "foreground_buff_icons.hpp"
#include "native_buff_icons.hpp"
#include "caption_free_buff_icons.hpp"
#include "uncaptioned_buff_icons.hpp"
#include <algorithm>

namespace hive::game::detail {
namespace {
BuffDefinition report(BuffType type,double maximum_noise_ratio=.4) {
    BuffDefinition result;result.type=type;
    result.value_kind=type.maximum==1?BuffValueKind::presence:BuffValueKind::stacks;
    for(const auto& icon:foreground_buff_icons)if(icon.name==type.name)result.appearances.push_back({&icon});
    for(const auto& icon:native_buff_icons)if(icon.name==type.name)result.appearances.push_back({&icon});
    for(const auto& icon:caption_free_buff_icons)if(icon.name==type.name) {
        BuffAppearance a{&icon,{3,2,32,16},-2,2,100};a.specificity=1;result.appearances.push_back(a);
    }
    for(auto& appearance:result.appearances)appearance.maximum_noise_ratio=maximum_noise_ratio;
    for(const auto& icon:uncaptioned_buff_icons)if(icon.name==type.name)
        result.single_stack=BuffAppearance{&icon,{3,22,32,16},-1,1,80};
    return result;
}
BuffDefinition summary(BuffType type,std::array<int,3> color,double tolerance,std::optional<BuffMeter> meter={},bool uncaptioned_single=true) {
    auto result=report(type);result.missing_is_unknown=true;
    result.value_kind=meter?BuffValueKind::meter:BuffValueKind::stacks;
    result.meter=meter;result.uncaptioned_single=uncaptioned_single;
    result.appearances.clear();
    for(const auto& icon:summary_buff_icons) if(icon.name==type.name) {
        BuffAppearance a{&icon,{2,2,34,28},-2,0,100};
        // Stable lower corners distinguish otherwise similar aura/meter art.
        a.specificity=2;
        a.normalization=meter?BuffNormalization::rows:BuffNormalization::whole;
        a.color_checks={{{1,37,3,1},color,tolerance},{{34,37,3,1},color,tolerance}};result.appearances.push_back(a);
    }
    for(const auto& icon:native_buff_icons)if(icon.name==type.name) {
        BuffAppearance a{&icon,{2,2,34,28},-2,0,100};
        a.specificity=2;
        // Low-resolution blue artwork bleeds into the neighboring palette.
        // Red/green remain stricter to distinguish the two balloon backgrounds.
        a.color_checks={{{1,34,3,2},color,{35,35,60}}};
        result.appearances.push_back(a);
    }
    for(const auto& icon:caption_free_buff_icons)if(icon.name==type.name) {
        BuffAppearance a{&icon,{3,2,32,16},-2,2,100};a.specificity=1;result.appearances.push_back(a);
    }
    return result;
}
BuffAppearance profile(const BuffProfile& profile) {
    BuffAppearance result{&profile.icon,{(38-profile.width)/2,31,profile.width,1},0,7,40};
    result.normalization=BuffNormalization::brightness;
    result.mean_color_limit=25;result.minimum_margin=.5;
    result.pixel_error_limit=8;result.upscaled_pixel_slack=20;
    return result;
}
}
std::span<const BuffDefinition> report_buff_definitions() {
    static const std::vector<BuffDefinition> definitions{
        report({"Blue Boost",10}),report({"Red Boost",10}),report({"Focus",10}),report({"Bomb Combo",10}),report({"Balloon Aura",10}),
        report({"Baby Love",1}),report({"Precision",10}),report({"Inspire",50}),report({"Honey Mark",3}),report({"Pollen Mark",3}),report({"Rage",4}),
        report({"White Boost",10}),report({"Pop Star",1}),report({"Melody",1},.3),report({"Jelly Bean Sharing",1}),
        report({"Guiding Star",1}),report({"Reindeer Fetch",10}),report({"Festive Mark",1}),
        summary({"Wealth Clock",5,"clock"},{226,172,53},30),
        summary({"Balloon Blessing",100,"balloon"},{200,202,60},30,{},false),
        summary({"Bubble Bloat",6,"bloat"},{72,128,204},18,BuffMeter{{72,128,204},1,5}),
        summary({"Mondo Blessing",10,"mondo"},{190,162,173},25),
        summary({"Tide Blessing",1,"tide"},{145,194,253},18,BuffMeter{{145,194,253},1.01,.19})
    };
    return definitions;
}
std::span<const BuffDefinition> movement_buff_definitions() {
    static const std::vector<BuffDefinition> definitions=[] {
        BuffDefinition haste;haste.type={"Haste",10};haste.value_kind=BuffValueKind::stacks;
        haste.appearances={{&foreground_buff_icons[18],{3,2,32,21},-2,2,80,{{{1,37,3,1},{240,240,240},55}}}};
        haste.appearances.front().normalization=BuffNormalization::rows;
        haste.appearances.front().minimum_edge_similarity=.62;
        haste.appearances.front().maximum_noise_ratio=.4;
        haste.single_stack=BuffAppearance{&uncaptioned_buff_icons[2],{3,22,32,16},-1,1,80};
        // The second identical icon is Coconut Haste, a flat bonus. Keeping the
        // ordered occurrence effects here avoids teaching the matcher game rules.
        haste.movement={{1,.1,0,10,false},{1,0,10,1,true}};
        BuffDefinition plus;plus.type={"Haste+",1};
        plus.appearances={{&foreground_buff_icons[19],{3,2,32,21},-2,2,80,{{{1,37,3,1},{237,219,76},40}}}};
        plus.appearances.front().normalization=BuffNormalization::rows;
        plus.appearances.front().minimum_edge_similarity=.62;
        plus.appearances.front().maximum_noise_ratio=.4;
        plus.movement={{2,0,0}};
        BuffDefinition oil;oil.type={"Oil",1};oil.appearances={profile(movement_buff_profiles[0])};oil.movement={{1.2,0,0}};
        BuffDefinition smoothie;smoothie.type={"Smoothie",1};smoothie.appearances={profile(movement_buff_profiles[1])};smoothie.movement={{1.25,0,0}};
        BuffDefinition bear;bear.type={"Bear morph",1};bear.movement={{1,0,4}};
        for(std::size_t i=2;i<movement_buff_profiles.size();++i)bear.appearances.push_back(profile(movement_buff_profiles[i]));
        return std::vector<BuffDefinition>{haste,plus,oil,smoothie,bear};
    }();
    return definitions;
}
}
