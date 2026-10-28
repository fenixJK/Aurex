#include "module.hpp"
#include <algorithm>

namespace hive::features::collection {
namespace {
const automation::Permissions permissions{automation::Permission::movement,automation::Permission::interact};
std::string item_key(bool blender,const std::string& item) {
    return std::string(blender?"blender-":"shrine-")+visits::selection_key(item).substr(7);
}
std::vector<extensions::Setting> item_settings(const Plan& plan) {
    std::vector<extensions::Setting> result{{"ant-purchases","Pass purchase limit",0,0,100}};
    result.back().help="For Ant quests when no pass is available. Each costs 10 tickets. 0 disables purchases; limit is per macro session.";
    if (std::any_of(plan.stations.begin(),plan.stations.end(),[](const auto& station) { return station.id=="Sticker Printer"; })) {
        result.push_back({"printer-egg","Egg",plan.items.printer_egg,0,0,{"Basic Egg","Silver Egg","Gold Egg","Diamond Egg","Mythic Egg"}});
        result.back().help="Uses one selected egg per print. It never substitutes another egg.";
    }
    for (const auto& station:plan.stations) {
        const bool blender=station.id=="Blender";
        if (!blender && station.id!="Wind Shrine") continue;
        const auto& catalog=blender?game::blender_recipes():game::shrine_items();
        result.push_back({blender?"blender-orders":"shrine-orders",blender?"Recipe":"Item",
            SettingSelection{blender?plan.items.recipe:plan.items.donation},0,0,catalog,true});
        for (const auto& item:catalog) {
            const auto key=item_key(blender,item);
            result.push_back({key+"-amount",blender?"Batches per craft":"Items per donation",1,1,999});
            if (blender && (item=="Gumdrops" || item=="Moon Charms")) result.back().help="Each batch produces 10 items.";
            result.push_back({key+"-runs",blender?"Craft limit":"Donation limit",1,1,999});
            result.back().help=blender?"Total crafts for this recipe. Each craft makes the batch quantity above.":"Total donations of this item. Each donation uses the quantity above.";
            result.push_back({key+"-unlimited","No limit",false});
        }
        if (!blender) { result.push_back({"shrine-hours","Interval",1,1,24}); result.back().suffix=" hr"; }
    }
    return result;
}
using Builder=std::function<std::unique_ptr<extensions::Module>(ItemOptions,const extensions::Manifest&)>;
class CollectionModule final : public extensions::Module {
    extensions::Manifest visits_;
    visits::AvailableSettings available_;
    std::vector<extensions::Setting> options_;
    ItemOptions defaults_;
    Builder build_;
    std::shared_ptr<ItemOrders> orders_;
    std::unique_ptr<extensions::Module> delegate_;
    unsigned ant_limit_{};
    std::map<game::ItemMenuKind,std::vector<ItemOrder>> queues_;
    bool connected_{};
public:
    CollectionModule(const extensions::Manifest& manifest,const Plan& plan,const Plan& available,Builder build,const extensions::Factory& initial)
        :visits_(manifest),available_(visit_plan(plan),visit_plan(available)),options_(item_settings(plan)),defaults_(plan.items),build_(std::move(build)),orders_(plan.items.orders) {
        for (const auto& option:options_) {
            const auto found=std::find_if(visits_.settings.begin(),visits_.settings.end(),[&](const auto& setting) { return setting.id==option.id; });
            if (found==visits_.settings.end() || found->initial.index()!=option.initial.index() || found->minimum!=option.minimum
                || found->maximum!=option.maximum || found->choices!=option.choices) throw std::invalid_argument("Invalid Collection item schema");
            extensions::validate_setting(*found,found->initial);
            visits_.settings.erase(found);
        }
        visits_=available_.manifest(std::move(visits_)); delegate_=initial(visits_);
    }
    void start(const Settings& values) override {
        auto settings=values;
        auto options=defaults_;
        for (const auto& option:options_) {
            const auto found=settings.find(option.id);
            if (found==settings.end()) throw std::invalid_argument("Missing Collection item preference");
            extensions::validate_setting(option,found->second);
            settings.erase(found);
        }
        queues_.clear();
        for (const bool blender:{true,false}) {
            const auto selection=values.find(blender?"blender-orders":"shrine-orders");
            if (selection==values.end()) continue;
            std::vector<ItemOrder> orders;
            for (const auto& item:std::get<SettingSelection>(selection->second)) {
                const auto key=item_key(blender,item);
                orders.push_back({item,unsigned(std::get<int>(values.at(key+"-amount"))),unsigned(std::get<int>(values.at(key+"-runs"))),std::get<bool>(values.at(key+"-unlimited"))});
            }
            queues_[blender?game::ItemMenuKind::blender:game::ItemMenuKind::wind_shrine]=std::move(orders);
        }
        if (values.contains("shrine-hours")) options.shrine_interval=std::chrono::hours(std::get<int>(values.at("shrine-hours")));
        if (values.contains("printer-egg")) options.printer_egg=std::get<std::string>(values.at("printer-egg"));
        ant_limit_=unsigned(std::get<int>(values.at("ant-purchases")));
        auto next=build_(std::move(options),visits_); next->start(available_.values(std::move(settings))); delegate_=std::move(next); connected_=false;
    }
    void update(const extensions::SessionView& view) override {
        if (!connected_) {
            for (const auto& [kind,orders]:queues_) orders_->configure(kind,orders);
            connected_=true;
        }
        if (defaults_.ant_passes) defaults_.ant_passes->configure(ant_limit_,view.session_id);
        delegate_->update(view);
    }
    std::vector<extensions::Metric> metrics() const override {
        auto result=delegate_->metrics(); const auto state=orders_->read();
        for (const bool blender:{true,false}) {
            const auto& queue=blender?state.blender:state.shrine;
            for (const auto& p:queue.orders) {
                result.push_back({p.order.item,std::to_string(p.completed)+" / "+(p.order.unlimited?std::string("∞"):std::to_string(p.order.runs)),{},blender?"Blender":"Wind Shrine"});
            }
        }
        return result;
    }
    std::vector<extensions::Command> commands() const override {
        return {{"restart-blender","Restart queue",{},false,false,"Blender"},
            {"restart-shrine","Restart queue",{},false,false,"Wind Shrine"}};
    }
    void invoke(const std::string& command) override {
        if (command=="restart-blender") orders_->reset(game::ItemMenuKind::blender);
        else if (command=="restart-shrine") orders_->reset(game::ItemMenuKind::wind_shrine);
        else throw std::invalid_argument("Unknown Collection command");
    }
    std::vector<extensions::ModuleTimer> timers() const override { return delegate_->timers(); }
    std::optional<std::vector<extensions::Metric>> report_metrics() const override { return delegate_->report_metrics(); }
    std::vector<Counter> report_counters() const override { return delegate_->report_counters(); }
    void stop() noexcept override { if (connected_ && defaults_.ant_passes) defaults_.ant_passes->disable(); connected_=false; delegate_->stop(); }
    game::RequestBatch request_channels() const override { return delegate_->request_channels(); }
    std::optional<extensions::RoutineParticipation> routine() const override { return delegate_->routine(); }
    std::unique_ptr<automation::Program> routine_program() const override { return delegate_->routine_program(); }
    bool executable() const noexcept override { return delegate_->executable(); }
    bool pending_work(const extensions::SessionView& view) const override { return delegate_->pending_work(view); }
    std::optional<extensions::WorkEstimate> next_work(const extensions::SessionView& view) const override { return delegate_->next_work(view); }
    std::unique_ptr<automation::Program> program() const override { return delegate_->program(); }
};
}
Settings migrate_item_settings(Settings values) {
    for (const bool blender:{true,false}) {
        const auto old=values.find(blender?"blender-recipe":"shrine-item");
        if (old==values.end()) continue;
        const auto item=std::get<std::string>(old->second);
        const auto& catalog=blender?game::blender_recipes():game::shrine_items();
        if (std::find(catalog.begin(),catalog.end(),item)==catalog.end()) throw std::invalid_argument("Unknown saved Collection item");
        const auto amount=values.find(blender?"blender-batches":"shrine-quantity");
        values[blender?"blender-orders":"shrine-orders"]=SettingSelection{item};
        values[item_key(blender,item)+"-amount"]=amount==values.end()?1:std::get<int>(amount->second);
        values[item_key(blender,item)+"-unlimited"]=true;
        values.erase(old);
        values.erase(blender?"blender-batches":"shrine-quantity");
    }
    return values;
}
extensions::Manifest module_manifest(const Plan& plan,std::string id,std::string provider,bool requests) {
    auto manifest=features::visits::manifest(visit_plan(plan),std::move(id),std::move(provider),"Collection",permissions);
    manifest.setup_profiles={{"Honey farming",20,{{"interrupt-gather",false}},true,{},"Collect selected rewards between gathers."},
        {"Quest progress",30,{{"interrupt-gather",true}},true,{},"Handle requested quest visits and your selected pickups."},
        {"Materials",30,{{"interrupt-gather",true}},true,{},"Prioritize your selected pickups and crafting orders."}};
    manifest.enabled_by_default=true;
    if (requests) manifest.consumes.insert(extensions::RequestService::stations);
    for (const auto& station : plan.stations) for (auto& setting : manifest.settings)
        if (setting.id==features::visits::selection_key(station.id)) setting.initial=station.selected_by_default;
    extensions::PageSection pickups{"pickups","Pickups"}; pickups.tab="Pickups"; pickups.columns=3;
    extensions::PageSection games{"games","Memory Match"}; games.tab="Activities";
    for (const auto& station:plan.stations) {
        if (station.id=="Blender" || station.id=="Wind Shrine") continue;
        if (station.id=="Sticker Printer" || station.id=="Sticker Stack") continue;
        (station.kind==game::StationInteraction::memory_match?games:pickups).settings.push_back(features::visits::selection_key(station.id));
    }
    manifest.page->compact=true;
    auto& sections=manifest.page->sections; sections.clear();
    sections.push_back({"schedule","Options",{},{"interrupt-gather"},false,1});
    if (!pickups.settings.empty()) sections.push_back(std::move(pickups));
    if (!games.settings.empty()) sections.push_back(std::move(games));
    extensions::PageSection ant{"ant-purchases","Ant quests","0 uses free passes only. Each purchased pass costs 10 tickets.",{"ant-purchases"},false,1};
    ant.tab="Activities"; sections.push_back(std::move(ant));
    if (std::any_of(plan.stations.begin(),plan.stations.end(),[](const auto& station) { return station.id=="Sticker Printer"; })) {
        extensions::PageSection printer{"printer","Sticker Printer",{},{features::visits::selection_key("Sticker Printer"),"printer-egg"},false,2};
        printer.tab="Activities"; sections.push_back(std::move(printer));
    }
    if (std::any_of(plan.stations.begin(),plan.stations.end(),[](const auto& station) { return station.id=="Sticker Stack"; })) {
        const auto key=features::visits::selection_key("Sticker Stack");
        extensions::PageSection stack{"sticker-stack","Sticker Stack",{},{key},false,1};
        stack.tab="Activities"; sections.push_back(std::move(stack));
        for (auto& setting:manifest.settings) if (setting.id==key) {
            setting.label="Sticker Stack · 25 tickets";
            setting.help="Renews when the detected boost duration expires. Never donates stickers or skins.";
        }
    }
    for (const auto& station:plan.stations) {
        const bool blender=station.id=="Blender";
        if (!blender && station.id!="Wind Shrine") continue;
        const auto selection=features::visits::selection_key(station.id);
        for (auto& setting:manifest.settings) if (setting.id==selection) setting.label="Enabled";
        const std::string queue=blender?"blender-orders":"shrine-orders";
        extensions::PageSection controls{selection,station.id, {},{selection},false,2}; controls.tab=station.id;
        if (!blender) controls.settings.push_back("shrine-hours");
        sections.push_back(std::move(controls));
        extensions::PageSection list{queue,station.id,"Runs one of each in order, then repeats until each limit is reached.",{queue},false,1}; list.tab=station.id;
        sections.push_back(std::move(list));
        for (const auto& item:blender?game::blender_recipes():game::shrine_items()) {
            const auto key=item_key(blender,item);
            extensions::PageSection entry{key,"Order", {},{key+"-amount",key+"-runs",key+"-unlimited"},false,3};
            entry.collection=queue; entry.choice=item; sections.push_back(std::move(entry));
        }
    }
    for (auto option:item_settings(plan)) manifest.settings.push_back(std::move(option));
    for (auto& setting:manifest.settings) {
        if (setting.id=="printer-egg") setting.enabled_by=features::visits::selection_key("Sticker Printer");
        if (setting.id=="shrine-hours" || setting.id=="shrine-orders") setting.enabled_by=features::visits::selection_key("Wind Shrine");
        if (setting.id=="blender-orders") setting.enabled_by=features::visits::selection_key("Blender");
    }
    sections.push_back({"run","Run limit",{},{"run-seconds"}});
    for (auto& setting:manifest.settings) if (setting.id=="run-seconds") setting.hidden=true;
    sections.push_back({"overview","Progress",{},{},true,2,true});
    for (auto& section:sections) section.inline_labels=true;
    for (auto& setting:manifest.settings) {
        if (setting.label.ends_with(" Dispenser")) setting.label.erase(setting.label.size()-10);
        if (setting.id==features::visits::selection_key("Night Memory Match"))
            setting.help="Requires 30 bees and a Moon Amulet. Runs during observed nighttime.";
        if (setting.id==features::visits::selection_key("Mega Memory Match"))
            setting.help="Requires access to the Badge Bearer's Guild.";
    }
    return manifest;
}
extensions::Factory module_factory(Plan plan,automation::NavigationServices navigation,std::shared_ptr<const automation::AvailabilitySource> availability,std::shared_ptr<visits::Cooldowns> saved,std::shared_ptr<const game::RequestSource> requests,std::optional<Plan> configuration) {
    if (!plan.items.orders) plan.items.orders=std::make_shared<ItemOrders>();
    features::visits::Demand demand;
    if (requests) {
        std::map<std::string,std::string> targets;
        for (const auto& station:plan.stations) if (!station.action) targets.emplace(station.id,station.interaction.name);
        demand=[requests=std::move(requests),targets=std::move(targets)](const ObservationContext& context) {
            features::visits::RequestedVisits selected;
            const auto inbox=requests->channels(context.now,context.generation);
            if (const auto channel=inbox.find("stations"); channel!=inbox.end()) for (const auto& request:channel->second) {
                if (request.generation!=context.generation || request.expires<=context.now) continue;
                const auto& name=std::get<std::string>(request.data.at("station"));
                for (const auto& [id,station]:targets) if (station==name) selected[id].insert(request.owner);
            }
            return selected;
        };
    }
    const auto initial=features::visits::factory(visit_plan(plan),navigation,availability,permissions,demand,saved);
    if (configuration) configuration->items=plan.items;
    const auto display=std::make_shared<const Plan>(configuration?std::move(*configuration):plan);
    const auto defaults=std::make_shared<const Plan>(std::move(plan));
    Builder build=[defaults,navigation=std::move(navigation),availability=std::move(availability),demand=std::move(demand),saved=std::move(saved)](ItemOptions items,const extensions::Manifest& manifest) {
        auto configured=*defaults; configured.items=std::move(items); configure_items(configured);
        return features::visits::factory(visit_plan(std::move(configured)),navigation,availability,permissions,demand,saved)(manifest);
    };
    return [display,defaults,build=std::move(build),initial](const extensions::Manifest& manifest) { return std::make_unique<CollectionModule>(manifest,*display,*defaults,build,initial); };
}
}
