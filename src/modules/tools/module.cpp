#include "module.hpp"
#include "rolling.hpp"
#include <algorithm>
#include <cctype>

namespace hive::features::tools {
namespace {
std::string key_for(const std::string& name,const std::string& prefix) {
    auto key=prefix+name;
    std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c) { return c==' '?'-':char(std::tolower(c)); });
    return key;
}
MutationFilter mutation_filter(const Settings& settings) {
    MutationFilter filter;
    for (const auto& type:mutation_types()) {
        const auto key=key_for(type.name,"mutation-");
        if (std::get<bool>(settings.at(key))) filter.push_back({type.name,double(std::get<int>(settings.at(key+"-minimum"))),
            std::get<std::string>(settings.at(key+"-unit"))=="%"});
    }
    return filter;
}
class Tools final : public extensions::Module {
    extensions::Manifest manifest_;
    Settings settings_;
    std::shared_ptr<const game::InventorySource> inventory_;
    std::shared_ptr<RollProgress> progress_{std::make_shared<RollProgress>()};
public:
    Tools(extensions::Manifest manifest,std::shared_ptr<const game::InventorySource> inventory):manifest_(std::move(manifest)),inventory_(std::move(inventory)) {}
    void start(const Settings& settings) override { extensions::validate_settings(manifest_,settings); validate(mutation_filter(settings)); settings_=settings; }
    void update(const extensions::SessionView&) override {}
    void stop() noexcept override {}
    bool executable() const noexcept override { return true; }
    std::vector<extensions::Command> commands() const override {
        extensions::Command jelly{"jelly","Start mutation rolling",{},false,true,"Royal Jelly"};
        if (!settings_.empty() && std::get<bool>(settings_.at("jelly-neon")))
            jelly.target_prompt="Click the bee to reroll. It will receive Neonberries and Royal Jelly. Escape cancels.";
        return {std::move(jelly),
            {"bitter","Select bee and feed",{},false,true,"Bitterberries","Click the bee you want to mutate. Escape cancels without feeding."},
            {"ssa","Roll SSA",{},false,true,"SSA Roller"},
            {"hatch-basic","Select bee and hatch",{},false,true,"Basic Bee","Select the bee to replace with Basic Eggs. Each non-gifted Basic Bee uses one Royal Jelly before the next egg. Escape cancels."}};
    }
    std::unique_ptr<automation::Program> command_program(const std::string& command) const override { return command_program(command,{}); }
    std::unique_ptr<automation::Program> command_program(const std::string& command,const extensions::CommandInput& input) const override {
        if (command=="hatch-basic") {
            if (!input.target || !inventory_) throw std::invalid_argument("Select a bee with inventory available");
            return hatch_basic({*input.target,inventory_,unsigned(std::get<int>(settings_.at("egg-limit"))),
                std::get<bool>(settings_.at("egg-keep-gifted")),std::get<bool>(settings_.at("egg-keep-mythic")),std::get<bool>(settings_.at("egg-start-jelly"))},progress_);
        }
        if (command=="ssa") {
            SsaPlan plan;
            plan.filter.main=std::get<std::string>(settings_.at("ssa-main"));
            plan.filter.double_passive=std::get<bool>(settings_.at("ssa-double"));
            const auto& sides=std::get<SettingSelection>(settings_.at("ssa-sides"));
            plan.filter.sides={sides.begin(),sides.end()};
            const auto& required=std::get<SettingSelection>(settings_.at("ssa-required-stats"));
            for (const auto& stat:amulet_stats()) {
                const int value=std::get<int>(settings_.at(key_for(stat.name,"ssa-")));
                if (value) plan.filter.minimums[stat.name]=stat.multiplier?value/100.:double(value);
                else if (std::find(required.begin(),required.end(),stat.name)!=required.end()) plan.filter.minimums[stat.name]=stat.minimum;
            }
            plan.honey_billions=std::get<int>(settings_.at("ssa-limit"));
            return roll_ssa(std::move(plan),progress_);
        }
        if (command=="bitter") {
            if (!input.target || !inventory_) throw std::invalid_argument("Select a bee with inventory available");
            BitterPlan plan; plan.target=*input.target; plan.inventory=inventory_; plan.mutations=mutation_filter(settings_);
            plan.batch=std::get<int>(settings_.at("bitter-batch")); plan.limit=std::get<int>(settings_.at("bitter-limit"));
            plan.neon=std::get<bool>(settings_.at("bitter-neon")); plan.neon_limit=std::get<int>(settings_.at("neon-limit"));
            return roll_mutations(std::move(plan),progress_);
        }
        if (command=="jelly") {
            JellyPlan plan;
            plan.rolls=std::get<int>(settings_.at("jelly-limit"));
            plan.mutations=mutation_filter(settings_);
            plan.neon=std::get<bool>(settings_.at("jelly-neon"));
            plan.neon_limit=std::get<int>(settings_.at("jelly-neon-limit")); plan.target=input.target; plan.inventory=inventory_;
            return roll_jelly(std::move(plan),progress_);
        }
        throw std::invalid_argument("Unknown tool");
    }
    std::vector<extensions::Metric> metrics() const override {
        const auto report=progress_->read();
        std::vector<extensions::Metric> result{{"Status",report.status},{"Attempts",std::to_string(report.attempts)}};
        if (report.bitterberries || report.neonberries) { result.push_back({"Bitterberries",std::to_string(report.bitterberries)}); result.push_back({"Neonberries",std::to_string(report.neonberries)}); }
        if (!report.result.empty()) result.push_back({"Last result",report.result});
        return result;
    }
    std::map<std::string,std::string> setting_summaries() const override {
        const auto filters=mutation_filter(settings_); std::string mutations;
        for(const auto& filter:filters) {
            if(!mutations.empty())mutations+=" or ";
            mutations+=filter.minimum>0?describe({filter.name,filter.minimum,filter.percent}):filter.name;
        }
        if(mutations.empty())mutations="Any mutation";
        return {{"stops","Stop on: "+mutations},{"bitter-run","Stop on: "+mutations}};
    }
    std::optional<std::vector<extensions::Metric>> report_metrics() const override { return std::vector<extensions::Metric>{}; }
};
}
extensions::Manifest module_manifest() {
    extensions::Manifest m; m.id="hive.native-tools"; m.provider="native-tools"; m.name="Tools"; m.version="0.1.0";
    m.settings_version=4;
    m.description="Start individual bee and amulet tools here. Tools are not part of the farming rotation.";
    m.permissions={automation::Permission::interact};
    m.settings={{"egg-limit","Egg limit",100,1,1000000},{"egg-start-jelly","Starting bee is Basic",false},{"egg-keep-gifted","Keep any gifted",true},{"egg-keep-mythic","Keep any mythic",true},
        {"jelly-neon","Automatic Neonberries",false},{"jelly-neon-limit","Neonberry limit",1,1,1000},
        {"bitter-batch","Berries per feed",30,1,1000000},{"bitter-limit","Total berry limit",1000,1,1000000},
        {"bitter-neon","Automatic Neonberries",false},{"neon-limit","Neonberry limit",1,1,1000},
        {"jelly-limit","Button press limit",100,1,1000000},
        {"ssa-main","Main passive",std::string("Pop Star"),0,0,passives()},
        {"ssa-sides","Side passives",SettingSelection{},0,0,passives()},
        {"ssa-double","Double passive · 500B / roll",false},{"ssa-limit","Honey budget",500,10,1000000}};
    for (auto& setting:m.settings) {
        if (setting.id=="ssa-limit") {
            setting.suffix=" B"; setting.help="Maximum honey spent this run. Single rolls cost 10B; double rolls cost 500B.";
            setting.step=10; setting.step_toggle="ssa-double"; setting.checked_step=500;
        }
        if (setting.id=="ssa-sides") setting.inline_choices=true;
        if (setting.id=="ssa-sides") { setting.enabled_by="ssa-double"; setting.excluded_by="ssa-main"; setting.help="Choose acceptable second passives. The main passive is unavailable here. Leave empty to accept any second passive. Either order matches."; }
        if (setting.id=="neon-limit") setting.enabled_by="bitter-neon";
        if (setting.id=="jelly-neon-limit") setting.enabled_by="jelly-neon";
        if (setting.id=="jelly-neon") setting.help="Select a bee, feed one Neonberry, then start rolling. Refreshes after 11 minutes, up to your Neonberry limit.";
        if (setting.id=="jelly-limit") setting.help="Maximum Royal Jelly button presses by Aurex. Each press can spend many jellies with in-game Auto-Jelly.";
    }
    std::vector<std::string> stat_names; for (const auto& stat:amulet_stats()) stat_names.push_back(stat.name);
    m.settings.push_back({"ssa-required-stats","Stats to keep",SettingSelection{},0,0,stat_names});
    m.settings.back().inline_choices=true;
    m.settings.back().help="Choose up to five stats. Set advanced minimums only when you need specific values.";
    m.page=extensions::PageLayout{{},true}; auto& sections=m.page->sections;
    auto section=[&](std::string id,std::string title,std::vector<std::string> settings,std::string tab,unsigned columns=2,std::string description={}) {
        sections.push_back({std::move(id),std::move(title),std::move(description),std::move(settings),false,columns}); sections.back().tab=std::move(tab);
    };
    section("jelly-help","Royal Jelly",{},"Royal Jelly",1,"Choose your bee and gifted filters and enable Require Mutation in Roblox. Open a Royal Jelly result to begin, or enable Neonberries to select a bee. Esc stops the tool.");
    section("stops","Rolling",{"jelly-limit"},"Royal Jelly",1,"One button press can spend many jellies with in-game Auto-Jelly.");
    section("jelly-neon","Neonberries",{"jelly-neon","jelly-neon-limit"},"Royal Jelly",2,"Feed one before rolling and refresh after 11 minutes, up to your limit.");
    section("mutation-help","Mutation filters",{},"Royal Jelly",1,"Shared by Royal Jelly and Bitterberries. Stops on any selected mutation at or above its minimum. No selection accepts any mutation; minimum 0 accepts any value and unit.");
    sections.back().tool="Mutation filters";
    for (const auto& type:mutation_types()) {
        const auto key=key_for(type.name,"mutation-");
        m.settings.push_back({key,"Accept",false});
        m.settings.push_back({key+"-minimum","Minimum",0,0,1000000});
        m.settings.back().enabled_by=key;
        std::vector<std::string> units;if (type.flat) units.push_back("+");if (type.percent) units.push_back("%");
        m.settings.push_back({key+"-unit","Unit",units.front(),0,0,units});
        m.settings.back().enabled_by=key;
        section(key,type.name,{key,key+"-minimum",key+"-unit"},"Royal Jelly",3);sections.back().row=true; sections.back().tool="Mutation filters";
    }
    section("bitter-run","Bitterberries",{"bitter-batch","bitter-limit"},"Bitterberries",2,"Face your hive, choose a mutation filter, then select a bee.");
    section("neon","Neonberries",{"bitter-neon","neon-limit"},"Bitterberries",2,"Feed one before rolling and refresh after 11 minutes, up to your limit.");
    section("bitter-filter-link","Mutation filters",{},"Bitterberries",1); sections.back().tool="Mutation filters";
    section("ssa-passives","SSA Roller",{"ssa-main","ssa-limit","ssa-double"},"SSA Roller",2,"Start at the SSA generator or an open result. Matches stay open. Escape stops.");
    section("ssa-sides","Second passive",{"ssa-sides"},"SSA Roller",1);
    std::vector<std::string> keys;
    for (const auto& stat:amulet_stats()) {
        const auto key=key_for(stat.name,"ssa-"); keys.push_back(key);
        m.settings.push_back({key,stat.name,0,0,int(stat.maximum*(stat.multiplier?100:1))});
        m.settings.back().suffix=stat.multiplier?"×":"%"; m.settings.back().decimals=stat.multiplier?2:0;
        m.settings.back().help="0 uses the selection above. A positive minimum requires this stat even if its checkbox is off.";
    }
    section("ssa-required","Stats to keep",{"ssa-required-stats"},"SSA Roller",1);
    section("ssa-stats","Stat minimums",keys,"SSA Roller",2); sections.back().tool="Stat minimums";
    section("basic-bee","Gifted Basic Bee",{"egg-limit","egg-start-jelly","egg-keep-gifted","egg-keep-mythic"},"Basic Bee",2,
        "Rolls Basic Eggs with Royal Jelly between Basic results. Stops on Gifted Basic or selected rare results. Keep in-game Auto-Jelly off.");
    sections.push_back({"progress","Last result",{},{},true,2});
    sections.back().collapsed=true;
    for (auto& section:sections) section.inline_labels=true;
    return m;
}
extensions::Factory module_factory(std::shared_ptr<const game::InventorySource> inventory) {
    return [inventory=std::move(inventory)](const extensions::Manifest& manifest) { return std::make_unique<Tools>(manifest,inventory); };
}
Settings migrate_settings(int version,Settings settings) {
    if (version<1 || version>4) throw std::invalid_argument("Unsupported tools settings version");
    for (const auto* key:{"ssa-delay","jelly-delay","bitter-delay","bees","gifted","mythic","jelly-mutations"}) settings.erase(key);
    if (version<3) if (const auto limit=settings.find("ssa-limit");limit!=settings.end())
        if (auto* value=std::get_if<int>(&limit->second)) *value=std::max(10,*value);
    return settings;
}
}
