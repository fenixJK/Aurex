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
        extensions::Command jelly{"jelly","Roll Royal Jelly",{},false,true,"Bee Genetics"};
        if (!settings_.empty() && std::get<bool>(settings_.at("jelly-mutations")) && std::get<bool>(settings_.at("jelly-neon")))
            jelly.target_prompt="Click the bee to reroll. It will receive Neonberries and Royal Jelly. Escape cancels.";
        return {std::move(jelly),
            {"bitter","Select bee and feed",{},false,true,"Bitterberries","Click the bee you want to mutate. Escape cancels without feeding."},
            {"ssa","Roll SSA",{},false,true,"SSA Roller"}};
    }
    std::unique_ptr<automation::Program> command_program(const std::string& command) const override { return command_program(command,{}); }
    std::unique_ptr<automation::Program> command_program(const std::string& command,const extensions::CommandInput& input) const override {
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
            plan.delay=Time(std::get<int>(settings_.at("bitter-delay")));
            return roll_mutations(std::move(plan),progress_);
        }
        if (command=="jelly") {
            JellyPlan plan;
            const auto& selected=std::get<SettingSelection>(settings_.at("bees")); plan.filter.names={selected.begin(),selected.end()};
            plan.filter.stop_gifted=std::get<bool>(settings_.at("gifted")); plan.filter.stop_mythic=std::get<bool>(settings_.at("mythic"));
            plan.rolls=std::get<int>(settings_.at("jelly-limit")); plan.delay=Time(std::get<int>(settings_.at("jelly-delay")));
            plan.require_mutation=std::get<bool>(settings_.at("jelly-mutations")); plan.mutations=mutation_filter(settings_);
            plan.neon=plan.require_mutation && std::get<bool>(settings_.at("jelly-neon"));
            plan.neon_limit=std::get<int>(settings_.at("jelly-neon-limit")); plan.target=input.target; plan.inventory=inventory_;
            return roll_jelly(std::move(plan),progress_);
        }
        throw std::invalid_argument("Unknown tool");
    }
    std::vector<extensions::Metric> metrics() const override {
        const auto report=progress_->read();
        std::vector<extensions::Metric> result{{"Status",report.status},{"Rolls",std::to_string(report.attempts)}};
        if (report.bitterberries || report.neonberries) { result.push_back({"Bitterberries",std::to_string(report.bitterberries)}); result.push_back({"Neonberries",std::to_string(report.neonberries)}); }
        if (!report.result.empty()) result.push_back({"Last result",report.result});
        return result;
    }
    std::optional<std::vector<extensions::Metric>> report_metrics() const override { return std::vector<extensions::Metric>{}; }
};
}
extensions::Manifest module_manifest() {
    extensions::Manifest m; m.id="hive.native-tools"; m.provider="native-tools"; m.name="Tools"; m.version="0.1.0";
    m.settings_version=2;
    m.description="Start individual bee and amulet tools here. Tools are not part of the farming rotation.";
    m.permissions={automation::Permission::interact};
    m.settings={{"bees","Bees to keep",SettingSelection{},0,0,bees()},
        {"gifted","Any gifted",true},{"mythic","Any mythic",true},
        {"jelly-mutations","Mutation rolling",false},
        {"jelly-neon","Automatic Neonberries",false},{"jelly-neon-limit","Neonberry limit",1,1,1000},
        {"bitter-batch","Berries per feed",30,1,1000000},{"bitter-limit","Total berry limit",1000,1,1000000},
        {"bitter-neon","Use Neonberries",false},{"neon-limit","Neonberry limit",1,1,1000},{"bitter-delay","Feed delay",800,500,5000},
        {"jelly-limit","Roll limit",100,1,1000000},{"jelly-delay","Roll delay",800,500,5000},
        {"ssa-main","Main passive",std::string("Pop Star"),0,0,passives()},
        {"ssa-sides","Side passives",SettingSelection{},0,0,passives()},
        {"ssa-double","Double passive · 500B / roll",false},{"ssa-limit","Honey budget",500,10,1000000}};
    for (auto& setting:m.settings) {
        if (setting.id.ends_with("delay")) setting.suffix=" ms";
        if (setting.id=="ssa-limit") {
            setting.suffix=" B"; setting.help="Maximum honey spent this run. Single rolls cost 10B; double rolls cost 500B.";
            setting.step=10; setting.step_toggle="ssa-double"; setting.checked_step=500;
        }
        if (setting.id=="bees" || setting.id=="ssa-sides") setting.inline_choices=true;
        if (setting.id=="bees") for (const auto& bee:setting.choices) {
            auto name=bee; std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c) { return char(std::tolower(c)); });
            setting.icons.emplace(bee,":/bees/"+name+".png");
        }
        if (setting.id=="ssa-sides") { setting.enabled_by="ssa-double"; setting.help="Choose acceptable second passives, different from the main. Leave empty to accept any second passive. Either order matches."; }
        if (setting.id=="neon-limit") setting.enabled_by="bitter-neon";
        if (setting.id=="jelly-neon-limit") setting.enabled_by="jelly-neon";
        if (setting.id=="jelly-neon") setting.help="When mutation rolling, feed one Neonberry before rolling and refresh after 11 minutes. Stops at your Neonberry limit.";
        if (setting.id=="jelly-mutations") setting.help="Use Royal Jelly until both the bee and mutation match. No bees selected accepts any species. Any gifted and Any mythic still stop independently.";
        if (setting.id=="jelly-limit") setting.help="Limits button presses. Turn off in-game Auto-Jelly if you want one jelly per press.";
    }
    std::vector<std::string> stat_names; for (const auto& stat:amulet_stats()) stat_names.push_back(stat.name);
    m.settings.push_back({"ssa-required-stats","Stats to keep",SettingSelection{},0,0,stat_names});
    m.settings.back().inline_choices=true;
    m.page=extensions::PageLayout{{},true}; auto& sections=m.page->sections;
    auto section=[&](std::string id,std::string title,std::vector<std::string> settings,std::string tab,unsigned columns=2,std::string description={}) {
        sections.push_back({std::move(id),std::move(title),std::move(description),std::move(settings),false,columns}); sections.back().tab=std::move(tab);
    };
    section("jelly-help","Bee Genetics",{},"Bee Genetics",1,"Choose bees and optional mutations. Start from a Royal Jelly result, or select a bee with Automatic Neonberries enabled.");
    section("bees","Bees to keep",{"bees"},"Bee Genetics",1);
    section("stops","Stop on",{"gifted","mythic","jelly-mutations"},"Bee Genetics",3);
    section("jelly-neon","Radioactive",{"jelly-neon","jelly-neon-limit"},"Bee Genetics");
    section("jelly-run","Limits",{"jelly-limit","jelly-delay"},"Bee Genetics");
    section("mutation-help","Mutation filters",{},"Bee Genetics",1,"Shared by mutation rolling and Bitterberries. No selection accepts any mutation; minimum 0 accepts any value and unit.");
    sections.back().tool="Mutation filters";
    for (const auto& type:mutation_types()) {
        const auto key=key_for(type.name,"mutation-");
        m.settings.push_back({key,"Use",false});
        m.settings.push_back({key+"-minimum","Minimum",0,0,1000000});
        m.settings.back().enabled_by=key;
        std::vector<std::string> units;if (type.flat) units.push_back("+");if (type.percent) units.push_back("%");
        m.settings.push_back({key+"-unit","Unit",units.front(),0,0,units});
        m.settings.back().enabled_by=key;
        section(key,type.name,{key,key+"-minimum",key+"-unit"},"Bee Genetics",3);sections.back().row=true; sections.back().tool="Mutation filters";
    }
    section("bitter-run","Bitterberries",{"bitter-batch","bitter-limit"},"Bitterberries",2,"Keep the current bee and reroll its mutation. Face your hive, choose mutations, then select a bee. Esc stops.");
    section("neon","Radioactive",{"bitter-neon","neon-limit"},"Bitterberries",2,"Uses one Neonberry before feeding and refreshes it after 11 minutes, up to your limit.");
    section("bitter-timing","Feed delay",{"bitter-delay"},"Bitterberries",1);
    section("bitter-filter-link","Mutation filters",{},"Bitterberries",1); sections.back().tool="Mutation filters";
    section("ssa-passives","SSA Roller",{"ssa-main","ssa-double"},"SSA Roller",2,"Start at the SSA generator or an open result. Matches stay open. Escape stops.");
    section("ssa-sides","Second passive · none selected accepts any",{"ssa-sides"},"SSA Roller",1);
    std::vector<std::string> keys;
    for (const auto& stat:amulet_stats()) {
        const auto key=key_for(stat.name,"ssa-"); keys.push_back(key);
        m.settings.push_back({key,stat.name,0,0,int(stat.maximum*(stat.multiplier?100:1))});
        m.settings.back().suffix=stat.multiplier?"×":"%"; m.settings.back().decimals=stat.multiplier?2:0;
        m.settings.back().help="0 uses the selection above. A positive minimum requires this stat even if its checkbox is off.";
    }
    section("ssa-required","Stats to keep",{"ssa-required-stats"},"SSA Roller",1,"Choose up to five stats. Set minimums below only if you need specific values.");
    section("ssa-stats","Advanced · stat minimums",keys,"SSA Roller",3); sections.back().collapsed=true;
    section("ssa-run","Limits",{"ssa-limit"},"SSA Roller",2);
    sections.push_back({"progress","Last result",{},{},true,2});
    sections.back().collapsed=true;
    return m;
}
extensions::Factory module_factory(std::shared_ptr<const game::InventorySource> inventory) {
    return [inventory=std::move(inventory)](const extensions::Manifest& manifest) { return std::make_unique<Tools>(manifest,inventory); };
}
}
