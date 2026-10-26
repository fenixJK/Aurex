#include "module.hpp"
#include "game/data/fields.hpp"
#include <iomanip>
#include <sstream>

namespace hive::features::vichop {
namespace {
class Module final : public extensions::Module {
    extensions::Manifest manifest_;
    Services services_;
    Settings settings_;
    std::string activity_;
    std::shared_ptr<Controller> controller_;
public:
    Module(extensions::Manifest manifest,Services services):manifest_(std::move(manifest)),services_(std::move(services)),controller_(std::make_shared<Controller>(manifest_.id)) {}
    void start(const Settings& settings) override { extensions::validate_settings(manifest_,settings); settings_=settings; }
    void update(const extensions::SessionView& view) override {
        controller_->update(view);
        activity_=view.feature && view.feature->module_id==manifest_.id?view.feature->status.text():std::string{};
        if (services_.servers) services_.servers->update(view.now);
    }
    void stop() noexcept override { controller_->stop(); if (services_.servers) services_.servers->set_active(false); }
    bool executable() const noexcept override { return true; }
    std::vector<extensions::Command> commands() const override { return {{"start","Start Vic Hop",{},false,true}}; }
    game::RequestBatch request_channels() const override { return controller_->requests(); }
    std::unique_ptr<automation::Program> command_program(const std::string& id) const override {
        if (id!="start") throw std::invalid_argument("Unknown Vic Hop command");
        auto services=services_; services.enemy.hunt_fields.clear();
        for (const auto& name:std::get<SettingSelection>(settings_.at("fields"))) {
            const auto found=std::find_if(game::fields().begin(),game::fields().end(),[&](const auto& field) { return field.name==name; });
            if (found==game::fields().end() || std::find(services_.enemy.hunt_fields.begin(),services_.enemy.hunt_fields.end(),found->id)==services_.enemy.hunt_fields.end())
                throw std::invalid_argument("Select reachable Vicious Bee fields");
            services.enemy.hunt_fields.push_back(found->id);
        }
        services.enemy.fight.duration=std::chrono::minutes(std::get<int>(settings_.at("fight-minutes")));
        return vichop::program(std::move(services),controller_,std::chrono::minutes(std::get<int>(settings_.at("minutes"))));
    }
    std::vector<extensions::Metric> metrics() const override {
        const auto state=controller_->read();
        std::ostringstream rate;
        if (state.elapsed>=Time(1000)) rate<<std::fixed<<std::setprecision(1)<<state.kills*3600000.0/state.elapsed.count()<<" / hour";

        return {{"Status",state.active && !activity_.empty()?activity_:state.status},
            {"Confirmed kills",std::to_string(state.kills)+(rate.str().empty()?"":"  ·  "+rate.str())},
            {"Servers",std::to_string(state.servers)+" joined  ·  "+std::to_string(state.nights)+" night  ·  "+std::to_string(state.skipped)+" skipped"},
            {"Searches",std::to_string(state.misses)+" without a kill  ·  "+std::to_string(state.failures)+" interrupted"},
            {"Last result",state.last_result}};
    }
    std::vector<Counter> report_counters() const override {
        const auto state=controller_->read();
        return {{"vicious-kills","Vicious Bee kills",state.kills,state.epoch},
            {"servers","Servers visited",state.servers,state.epoch}};
    }
};
}
extensions::Manifest module_manifest(const combat::Plan& plan) {
    extensions::Manifest m; m.id="hive.native-vichop"; m.provider="native-vichop"; m.name="Vic Hop"; m.version="0.1.0";
    m.enabled_by_default=true;
    m.description="A standalone Vicious Bee hunting tool. Joins public servers, searches confirmed night servers and fights Vicious Bee.";
    m.permissions={automation::Permission::movement,automation::Permission::interact,automation::Permission::tool};
    m.requests={extensions::RequestService::servers};
    std::vector<std::string> names;
    for (const auto& enemy:plan.enemies) if (enemy.species=="Vicious Bee")
        for (const auto& id:enemy.hunt_fields) for (const auto& field:game::fields()) if (field.id==id) names.push_back(field.name);
    m.settings={{"fields","Search fields",SettingSelection(names),0,0,names},{"minutes","Run time",120,1,480},
        {"fight-minutes","Fight limit",5,1,10}};
    m.settings[0].ordered=true; m.settings[0].help="Search reachable fields in this order using Vicious Bee combat notifications.";
    m.settings[1].suffix=" min"; m.settings[2].suffix=" min";
    m.page=extensions::PageLayout{{
        {"run","Vic Hop","Start this tool here. Start macro runs your normal modules. Stop or Esc ends the tool and leaves Roblox in its current server.",{"minutes","fight-minutes"}},
        {"fields","Fields","Select fields your hive can reach.",{"fields"},false,1},
        {"results","This run",{},{},true}},true};
    m.page->sections[1].collapsed=true;
    return m;
}
extensions::Factory module_factory(Services services) {
    return [services=std::move(services)](const extensions::Manifest& manifest) { return std::make_unique<Module>(manifest,services); };
}
}
