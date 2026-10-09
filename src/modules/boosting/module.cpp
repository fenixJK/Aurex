#include "module.hpp"
#include "game/data/stations.hpp"
#include "game/data/fields.hpp"
#include <algorithm>

namespace hive::features::boosting {
namespace {
const std::vector<std::string> conditions{"Off","Gathering","Boosted field","Field start","Converting","Bag limit","Boosted conversion"};
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
public:
    Boosting(extensions::Manifest manifest,std::shared_ptr<Controller> controller,
        std::shared_ptr<const game::DetectionSource> source,std::vector<Booster> boosters,std::vector<std::string> fields)
        :manifest_(std::move(manifest)),controller_(std::move(controller)),source_(std::move(source)),boosters_(std::move(boosters)),fields_(std::move(fields)) {}
    void start(const Settings& settings) override {
        extensions::validate_settings(manifest_,settings);
        Preferences preferences;
        preferences.follow=std::get<bool>(settings.at("follow")); preferences.interrupt=std::get<bool>(settings.at("interrupt-gather"));
        preferences.fields=std::get<SettingSelection>(settings.at("fields")); preferences.boosters=std::get<SettingSelection>(settings.at("boosters"));
        std::erase_if(preferences.fields,[&](const auto& field) { return std::find(fields_.begin(),fields_.end(),field)==fields_.end(); });
        for (unsigned i=0;i<7;++i) {
            const auto prefix="slot-"+std::to_string(i+1)+"-";
            const auto mode=std::get<std::string>(settings.at(prefix+"when"));
            preferences.items[i]={UseWhen(std::find(conditions.begin(),conditions.end(),mode)-conditions.begin()),
                std::chrono::seconds(std::get<int>(settings.at(prefix+"seconds"))),unsigned(std::get<int>(settings.at(prefix+"limit"))),
                unsigned(std::get<int>(settings.at(prefix+"bag")))};
        }
        const auto slot=[&](const char* id) { const auto& value=std::get<std::string>(settings.at(id)); return value=="Off"?0u:unsigned(std::stoi(value)); };
        preferences.boost={std::get<std::string>(settings.at("boost-field")),slot("dice-slot"),slot("glitter-slot"),
            unsigned(std::get<int>(settings.at("dice-limit"))),unsigned(std::get<int>(settings.at("glitter-limit"))),
            std::chrono::seconds(std::get<int>(settings.at("dice-interval"))),std::chrono::minutes(std::get<int>(settings.at("glitter-after")))};
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
        controller_->update(view,view.status.state==RunState::running && controller_->needs_reading()?source_->read():std::nullopt);
    }
    void stop() noexcept override { if (connected_) controller_->stop(); connected_=false; }
    std::vector<extensions::Metric> metrics() const override { return controller_->metrics(); }
    game::Requests requests() const override { return controller_->requests(now_,generation_); }
    std::optional<extensions::RoutineParticipation> routine() const override {
        return extensions::RoutineParticipation{75,Time{},false,true,true,preferences_.interrupt};
    }
    bool pending_work(const extensions::SessionView& view) const override { return controller_->next_booster(view.now,boosters_,requested_).has_value(); }
    bool executable() const noexcept override { return true; }
    std::unique_ptr<automation::Program> program() const override {
        const auto name=controller_->next_booster(now_,boosters_,requested_); if (!name) return {};
        const auto found=std::find_if(boosters_.begin(),boosters_.end(),[&](const auto& booster) { return booster.name==*name; });
        auto run=found->visit(); if (run) controller_->attempted(*name,now_); return run;
    }
};
}
extensions::Manifest module_manifest() {
    extensions::Manifest manifest; manifest.id="hive.native-boosting"; manifest.provider="native-boosting";
    manifest.name="Boosting"; manifest.version="1.0.0";
    manifest.description="Follow boosts, roll for a field and extend with glitter. Uses Gathering's field settings.";
    manifest.permissions={automation::Permission::movement,automation::Permission::interact};
    manifest.requests={extensions::RequestService::fields};
    manifest.consumes={extensions::RequestService::stations};
    manifest.routine=extensions::RoutineParticipation{75,Time{},false,true,true,true};
    std::vector<std::string> stations,fields;
    for (const auto& station:game::stations()) if (!station.boosted_fields.empty() && station.name!="Coconut Dispenser") stations.push_back(station.name);
    for (const auto& field:game::fields()) fields.push_back(field.name);
    manifest.settings={{"follow","Follow boosts",true},{"interrupt-gather","Gather interrupt",true},
        {"fields","Fields",SettingSelection(fields),0,0,fields,true},{"boosters","Boosters",SettingSelection{},0,0,stations,true}};
    manifest.page=extensions::PageLayout{{},true}; auto& sections=manifest.page->sections;
    sections.push_back({"follow","Boosts","Gather in boosted fields using their Gathering settings.",{"follow","interrupt-gather"},false,2}); sections.back().tab="Boosts";
    sections.push_back({"fields","Fields",{},{"fields"},false,1}); sections.back().tab="Boosts";
    sections.push_back({"boosters","Boosters",{},{"boosters"},false,1}); sections.back().tab="Boosts";
    for (auto& setting:manifest.settings) {
        if (setting.id=="fields" || setting.id=="boosters") { setting.inline_choices=true; }
        if (setting.id=="fields") setting.enabled_by="follow";
    }
    const std::vector<std::string> slots{"Off","1","2","3","4","5","6","7"};
    manifest.settings.push_back({"boost-field","Field",std::string("Pine Tree"),0,0,fields});
    manifest.settings.push_back({"dice-slot","Dice slot",std::string("Off"),0,0,slots});
    manifest.settings.back().help="Field, Smooth or Loaded Dice. Rolls while gathering here until this field is boosted.";
    manifest.settings.push_back({"dice-limit","Use limit",25,1,10000});
    manifest.settings.back().help="Maximum dice presses per macro session, including presses when out of dice.";
    manifest.settings.push_back({"dice-interval","Roll interval",3,2,10}); manifest.settings.back().suffix=" sec";
    manifest.settings.push_back({"glitter-slot","Glitter slot",std::string("Off"),0,0,slots});
    manifest.settings.back().help="Extends once per observed boost start. An existing boost with unknown age is left alone. Without dice, uses the selected follow fields.";
    manifest.settings.push_back({"glitter-limit","Use limit",1,1,1000}); manifest.settings.back().help="Maximum glitter presses per macro session.";
    manifest.settings.push_back({"glitter-after","Extend after",13,10,14}); manifest.settings.back().suffix=" min";
    sections.push_back({"dice","Dice",{},{"boost-field","dice-slot","dice-limit","dice-interval"},false,2}); sections.back().tab="Dice & glitter";
    sections.push_back({"glitter","Glitter",{},{"glitter-slot","glitter-limit","glitter-after"},false,2}); sections.back().tab="Dice & glitter";
    sections.push_back({"hotbar-help","Hotbar","Use items from the game hotbar. Limits count presses per session; 0 is unlimited.",{},false,1}); sections.back().tab="Hotbar";
    for (unsigned slot=1;slot<=7;++slot) {
        const auto prefix="slot-"+std::to_string(slot)+"-";
        manifest.settings.push_back({prefix+"when","When",std::string("Off"),0,0,conditions});
        manifest.settings.back().help="Boosted conversion requires a boost still active in the last gathered field.";
        manifest.settings.push_back({prefix+"seconds","Interval",600,1,86400}); manifest.settings.back().suffix=" sec";
        manifest.settings.push_back({prefix+"limit","Use limit",1,0,100000}); manifest.settings.back().help="Per session. 0 means no limit. Counts key presses, including an unavailable item.";
        manifest.settings.push_back({prefix+"bag","Bag %",90,1,100}); manifest.settings.back().suffix="%";
        manifest.settings.back().help="Used only by Bag limit rules. Set below Gathering's bag limit to use an item before returning.";
        sections.push_back({prefix+"rule","Slot "+std::to_string(slot),{},
            {prefix+"when",prefix+"seconds",prefix+"limit",prefix+"bag"},false,4}); sections.back().tab="Hotbar"; sections.back().row=true;
    }
    sections.push_back({"progress","Current boosts",{},{},true,2}); sections.back().tab="Boosts"; sections.back().collapsed=true;
    return manifest;
}
extensions::Factory module_factory(std::shared_ptr<Controller> controller,
    std::shared_ptr<const game::DetectionSource> observations,std::vector<Booster> boosters,std::vector<std::string> fields) {
    if (!controller || !observations) throw std::invalid_argument("Boosting needs buff readings and item control");
    return [controller=std::move(controller),observations=std::move(observations),boosters=std::move(boosters),fields=std::move(fields)](const extensions::Manifest& manifest) {
        return std::make_unique<Boosting>(manifest,controller,observations,boosters,fields);
    };
}
}
