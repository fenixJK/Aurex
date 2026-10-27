#include "module.hpp"
#include "game/data/stations.hpp"
#include "game/data/fields.hpp"
#include "game/detection/item_menu.hpp"
#include "game/actions/inventory/item_reserves.hpp"
#include <algorithm>

namespace hive::features::boosting {
namespace {
const std::vector<std::string> conditions{"Off","Gathering","Boosted field","Field start","Converting","Bag limit","Boosted conversion","Combat"};
class Boosting final : public extensions::Module {
    extensions::Manifest manifest_;
    std::shared_ptr<Controller> controller_;
    std::shared_ptr<const game::DetectionSource> source_;
    std::vector<Booster> boosters_;
    std::vector<std::string> fields_;
    Time now_{};
    std::uint64_t generation_{};
    Preferences preferences_;
    std::set<std::string> requested_;
    bool connected_{};
    std::shared_ptr<game::ItemStock> stock_;
    std::shared_ptr<const game::InventorySource> inventory_;
    mutable Time stock_retry_{};
    game::ItemAmounts costs() const {
        game::ItemAmounts result;
        if (preferences_.boost.dice_slot) result[preferences_.boost.dice_item]=1;
        if (preferences_.boost.glitter_slot) result["Glitter"]=1;
        for (const auto& rule:preferences_.items) if (rule.when!=UseWhen::off && !rule.item.empty()) result[rule.item]=1;
        return result;
    }
    bool needs_supplies(Time now) const { return stock_ && inventory_ && now>=stock_retry_ && !stock_->unread(costs()).empty(); }
public:
    Boosting(extensions::Manifest manifest,std::shared_ptr<Controller> controller,
        std::shared_ptr<const game::DetectionSource> source,std::vector<Booster> boosters,std::vector<std::string> fields,std::shared_ptr<game::ItemStock> stock,std::shared_ptr<const game::InventorySource> inventory)
        :manifest_(std::move(manifest)),controller_(std::move(controller)),source_(std::move(source)),boosters_(std::move(boosters)),fields_(std::move(fields)),stock_(std::move(stock)),inventory_(std::move(inventory)) {}
    void start(const Settings& settings) override {
        extensions::validate_settings(manifest_,settings);
        Preferences preferences;
        preferences.follow=std::get<bool>(settings.at("follow")); preferences.interrupt=std::get<bool>(settings.at("interrupt-gather"));
        preferences.during_boosts=std::get<bool>(settings.at("during-boosts")); preferences.pre_glitter=std::get<bool>(settings.at("pre-glitter"));
        preferences.fields=std::get<SettingSelection>(settings.at("fields")); preferences.boosters=std::get<SettingSelection>(settings.at("boosters"));
        std::erase_if(preferences.fields,[&](const auto& field) { return std::find(fields_.begin(),fields_.end(),field)==fields_.end(); });
        for (unsigned i=0;i<7;++i) {
            const auto prefix="slot-"+std::to_string(i+1)+"-";
            const auto mode=std::get<std::string>(settings.at(prefix+"when"));
            preferences.items[i]={UseWhen(std::find(conditions.begin(),conditions.end(),mode)-conditions.begin()),
                std::chrono::seconds(std::get<int>(settings.at(prefix+"seconds"))),unsigned(std::get<int>(settings.at(prefix+"limit"))),
                unsigned(std::get<int>(settings.at(prefix+"bag"))),std::get<std::string>(settings.at(prefix+"item"))};
            if (preferences.items[i].item=="Unspecified") preferences.items[i].item.clear();
        }
        const auto slot=[&](const char* id) { const auto& value=std::get<std::string>(settings.at(id)); return value=="Off"?0u:unsigned(std::stoi(value)); };
        preferences.boost={std::get<std::string>(settings.at("boost-field")),slot("dice-slot"),slot("glitter-slot"),
            unsigned(std::get<int>(settings.at("dice-limit"))),unsigned(std::get<int>(settings.at("glitter-limit"))),
            std::chrono::seconds(std::get<int>(settings.at("dice-interval"))),std::chrono::minutes(std::get<int>(settings.at("glitter-after")))};
        preferences.boost.dice_item=std::get<std::string>(settings.at("dice-item"));
        preferences.boost.target_stacks=unsigned(std::get<int>(settings.at("target-stacks")));
        preferences.boost.duration=std::chrono::minutes(std::get<int>(settings.at("boost-minutes")));
        preferences.boost.maintain=std::get<bool>(settings.at("maintain-boost"));
        if (std::find(fields_.begin(),fields_.end(),preferences.boost.field)==fields_.end()) preferences.boost.field.clear();
        validate(preferences);
        preferences_=std::move(preferences);
    }
    void update(const extensions::SessionView& view) override {
        if (!connected_) { controller_->configure(preferences_); connected_=true; }
        now_=view.now; generation_=view.generation;
        requested_.clear();
        if (const auto requests=view.requests.find(extensions::RequestService::stations); requests!=view.requests.end())
            for (const auto& request:requests->second) if (request.generation==view.generation && request.expires>view.now)
                requested_.insert(std::get<std::string>(request.data.at("station")));
        std::optional<Time> blue_remaining;
        if (preferences_.pre_glitter && std::find(preferences_.boosters.begin(),preferences_.boosters.end(),"Blue Field Booster")!=preferences_.boosters.end())
            for (const auto& booster:boosters_) if (booster.name=="Blue Field Booster" && booster.remaining) blue_remaining=booster.remaining();
        controller_->update(view,view.status.state==RunState::running && controller_->needs_reading()?source_->read():std::nullopt,blue_remaining);
    }
    void stop() noexcept override { if (connected_) controller_->stop(); connected_=false; }
    std::vector<extensions::Metric> metrics() const override { return controller_->metrics(); }
    std::vector<extensions::HotbarUse> hotbar_uses() const override {
        std::vector<extensions::HotbarUse> result;
        if (preferences_.boost.dice_slot) result.push_back({preferences_.boost.dice_slot,"Dice","Field dice"});
        if (preferences_.boost.glitter_slot) result.push_back({preferences_.boost.glitter_slot,"Glitter","Glitter"});
        for (unsigned i=0;i<preferences_.items.size();++i) if (preferences_.items[i].when!=UseWhen::off)
            result.push_back({i+1,{},"Consumable"});
        return result;
    }
    game::Requests requests() const override { return controller_->requests(now_,generation_); }
    std::optional<extensions::RoutineParticipation> routine() const override {
        return extensions::RoutineParticipation{75,Time{},false,true,true,preferences_.interrupt};
    }
    bool pending_work(const extensions::SessionView& view) const override { return needs_supplies(view.now) || controller_->next_booster(view.now,boosters_,requested_).has_value(); }
    std::optional<extensions::WorkWindow> protected_work(const extensions::SessionView& view) const override { return controller_->protected_work(view.now); }
    std::optional<extensions::WorkEstimate> next_work(const extensions::SessionView& view) const override {
        if (needs_supplies(view.now)) return extensions::WorkEstimate{"Check boost supplies",{},Time{},view.now,view.now};
        if (auto name=controller_->next_booster(view.now,boosters_,requested_)) return extensions::WorkEstimate{*name,{},Time{},view.now,{}};
        return {};
    }
    bool executable() const noexcept override { return true; }
    std::unique_ptr<automation::Program> program() const override {
        if (needs_supplies(now_)) { stock_retry_=now_+std::chrono::minutes(5); return automation::inspect_reserves(stock_,inventory_,costs()); }
        const auto name=controller_->next_booster(now_,boosters_,requested_); if (!name) return {};
        const auto found=std::find_if(boosters_.begin(),boosters_.end(),[&](const auto& booster) { return booster.name==*name; });
        auto run=found->visit(); if (run) controller_->attempted(*name,now_); return run;
    }
};
}
extensions::Manifest module_manifest() {
    extensions::Manifest manifest; manifest.id="hive.native-boosting"; manifest.provider="native-boosting";
    manifest.name="Boosting"; manifest.version="1.0.0";
    manifest.settings_version=4;
    manifest.description="Follow boosts, roll for a field and extend with glitter. Uses Gathering's field settings.";
    manifest.setup_profiles={{"Honey farming",15,{{"follow",true},{"interrupt-gather",true}},{},{},"Follow boosts in your selected fields. Keeps your item limits."},
        {"Quest progress",60,{},false},{"Materials",60,{},false}};
    manifest.permissions={automation::Permission::movement,automation::Permission::interact};
    manifest.requests={extensions::RequestService::fields};
    manifest.consumes={extensions::RequestService::stations};
    manifest.routine=extensions::RoutineParticipation{75,Time{},false,true,true,true};
    std::vector<std::string> stations,fields;
    for (const auto& station:game::stations()) if (!station.boosted_fields.empty() && station.name!="Coconut Dispenser") stations.push_back(station.name);
    for (const auto& field:game::fields()) fields.push_back(field.name);
    manifest.settings={{"follow","Follow boosts",true},{"interrupt-gather","Gather interrupt",true},
        {"fields","Fields",SettingSelection(fields),0,0,fields,true},{"boosters","Boosters",SettingSelection{},0,0,stations,true}};
    manifest.settings.push_back({"during-boosts","Use boosters during active boosts",false});
    manifest.settings.back().help="Use selected boosters when ready, even while following a boost. Selected fields, cooldowns and Gather interrupt still apply.";
    manifest.settings.push_back({"pre-glitter","Pre-glitter",false});
    manifest.settings.back().help="On an unboosted selected field, use glitter 10 minutes before the selected Blue Booster is ready. Uses the same glitter slot and session limit.";
    manifest.page=extensions::PageLayout{{},true}; auto& sections=manifest.page->sections;
    sections.push_back({"follow","Boosts","Gather in boosted fields using their Gathering settings.",{"follow","interrupt-gather"},false,2}); sections.back().tab="Boosts";
    sections.push_back({"fields","Fields",{},{"fields"},false,4}); sections.back().tab="Boosts";
    sections.push_back({"boosters","Boosters",{},{"boosters","during-boosts"},false,1}); sections.back().tab="Boosts";
    for (auto& setting:manifest.settings) {
        if (setting.id=="fields" || setting.id=="boosters") { setting.inline_choices=true; }
        if (setting.id=="fields") setting.enabled_by="follow";
    }
    const std::vector<std::string> slots{"Off","1","2","3","4","5","6","7"};
    manifest.settings.push_back({"boost-field","Field",std::string("Pine Tree"),0,0,fields});
    manifest.settings.push_back({"dice-slot","Dice slot",std::string("Off"),0,0,slots});
    manifest.settings.back().help="Field, Smooth or Loaded Dice. Stops after hitting this field; another dice replaces its boost. Higher targets need glitter or another boost source. Unreadable stacks do not trigger more rolls.";
    manifest.settings.push_back({"dice-item","Dice type",std::string("Field Dice"),0,0,{"Field Dice","Smooth Dice","Loaded Dice"}});
    manifest.settings.push_back({"target-stacks","Target stacks",1,1,4});
    manifest.settings.push_back({"maintain-boost","Maintain boost",false});
    manifest.settings.back().help="Refresh with eligible dice and glitter near expiry. Uses the visible boost timer; item and time limits still apply.";
    manifest.settings.push_back({"boost-minutes","Time limit",0,0,1440}); manifest.settings.back().suffix=" min";
    manifest.settings.back().help="Stops spending dice and glitter after this long from the first use. 0 is unlimited; item use limits still apply.";
    manifest.settings.push_back({"dice-limit","Use limit",25,1,10000});
    manifest.settings.back().help="Maximum dice presses per macro session, including presses when out of dice.";
    manifest.settings.push_back({"dice-interval","Roll interval",3,2,10}); manifest.settings.back().suffix=" sec";
    manifest.settings.push_back({"glitter-slot","Glitter slot",std::string("Off"),0,0,slots});
    manifest.settings.back().help="Extends near expiry when off cooldown. A new boost cycle is recognized from its timer or increased stacks. Without dice, uses the selected follow fields.";
    manifest.settings.push_back({"glitter-limit","Use limit",1,1,1000}); manifest.settings.back().help="Maximum glitter presses per macro session.";
    manifest.settings.push_back({"glitter-after","Extend after",13,10,14}); manifest.settings.back().suffix=" min";
    sections.push_back({"dice","Dice",{},{"boost-field","dice-slot","dice-item","target-stacks","dice-limit","dice-interval","boost-minutes","maintain-boost"},false,2}); sections.back().tab="Dice && glitter";
    sections.push_back({"glitter","Glitter",{},{"glitter-slot","glitter-limit","glitter-after","pre-glitter"},false,2}); sections.back().tab="Dice && glitter";
    sections.push_back({"hotbar-help","Hotbar","Use items from the game hotbar. Limits count presses per session; 0 is unlimited.",{},false,1}); sections.back().tab="Hotbar";
    for (unsigned slot=1;slot<=7;++slot) {
        const auto prefix="slot-"+std::to_string(slot)+"-";
        auto items=game::shrine_items(); items.insert(items.begin(),"Unspecified"); items.push_back("Super Smoothie");
        manifest.settings.push_back({prefix+"item","Item",std::string("Unspecified"),0,0,items});
        manifest.settings.push_back({prefix+"when","When",std::string("Off"),0,0,conditions});
        manifest.settings.back().help="Boosted conversion requires a boost still active in the last gathered field.";
        manifest.settings.push_back({prefix+"seconds","Interval",600,1,86400}); manifest.settings.back().suffix=" sec";
        manifest.settings.push_back({prefix+"limit","Use limit",1,0,100000}); manifest.settings.back().help="Per session. 0 means no limit. Counts key presses, including an unavailable item.";
        manifest.settings.push_back({prefix+"bag","Bag %",90,1,100}); manifest.settings.back().suffix="%";
        manifest.settings.back().help="Used only by Bag limit rules. Set below Gathering's bag limit to use an item before returning.";
        sections.push_back({prefix+"rule","Slot "+std::to_string(slot),{},
            {prefix+"item",prefix+"when",prefix+"seconds",prefix+"limit",prefix+"bag"},false,4}); sections.back().tab="Hotbar"; sections.back().row=true;
    }
    sections.push_back({"progress","Current boosts",{},{},true,2}); sections.back().tab="Boosts"; sections.back().collapsed=true;
    for (auto& field:manifest.settings) {
        const auto condition=[&](const std::string& source,std::vector<std::string> values) { field.enabled_by=source; field.enabled_values=std::move(values); };
        if (field.id=="dice-limit" || field.id=="dice-interval" || field.id=="target-stacks" || field.id=="boost-field" || field.id=="maintain-boost") condition("dice-slot",{"1","2","3","4","5","6","7"});
        if (field.id=="glitter-limit" || field.id=="glitter-after" || field.id=="pre-glitter") condition("glitter-slot",{"1","2","3","4","5","6","7"});
        for (unsigned slot=1;slot<=7;++slot) {
            const auto prefix="slot-"+std::to_string(slot)+"-";
            if (field.id==prefix+"seconds" || field.id==prefix+"limit") { auto active=conditions; std::erase(active,std::string("Off")); condition(prefix+"when",std::move(active)); }
            if (field.id==prefix+"bag") condition(prefix+"when",{"Bag limit"});
        }
    }
    for (auto& section:sections) section.inline_labels=true;
    return manifest;
}
Settings migrate_settings(int version,Settings settings) {
    if(version<1 || version>3) throw std::invalid_argument("Unsupported Boosting settings version");
    if(auto previous=settings.extract("blue-when-ready");!previous.empty()) {
        previous.key()="during-boosts"; settings.insert(std::move(previous));
    }
    return settings;
}
extensions::Factory module_factory(std::shared_ptr<Controller> controller,
    std::shared_ptr<const game::DetectionSource> observations,std::vector<Booster> boosters,std::vector<std::string> fields,std::shared_ptr<game::ItemStock> stock,std::shared_ptr<const game::InventorySource> inventory) {
    if (!controller || !observations) throw std::invalid_argument("Boosting needs buff readings and item control");
    return [controller=std::move(controller),observations=std::move(observations),boosters=std::move(boosters),fields=std::move(fields),stock=std::move(stock),inventory=std::move(inventory)](const extensions::Manifest& manifest) {
        return std::make_unique<Boosting>(manifest,controller,observations,boosters,fields,stock,inventory);
    };
}
}
