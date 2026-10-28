#include "module.hpp"
#include "defaults.hpp"
#include <algorithm>

namespace hive::features::gathering {
namespace {
const automation::Permissions permissions{automation::Permission::movement,automation::Permission::tool,automation::Permission::interact};
std::vector<std::string> choices(const PatternControls& controls) {
    if (controls.presets.empty() || controls.presets.size()>64 || controls.width<1 || controls.width>12
        || controls.minutes<1 || controls.minutes>120 || controls.backpack<1 || controls.backpack>100)
        throw std::invalid_argument("Invalid gathering pattern controls");
    std::vector<std::string> result;
    for (const auto& preset : controls.presets) {
        if (preset.name.empty() || preset.name.size()>80 || (bool(preset.generate)==bool(preset.strategy || preset.configured)) || std::find(result.begin(),result.end(),preset.name)!=result.end())
            throw std::invalid_argument("Invalid gathering pattern preset");
        result.push_back(preset.name);
    }
    if (std::find(result.begin(),result.end(),controls.initial)==result.end()) throw std::invalid_argument("Unknown default gathering pattern");
    return result;
}
std::string field_prefix(const std::string& field) {
    std::uint64_t hash=14695981039346656037ULL;
    for (unsigned char c : field) { hash^=c; hash*=1099511628211ULL; }
    return "field-"+std::to_string(hash);
}
std::vector<std::string> sprinkler_choices() {
    std::vector<std::string> result;
    for (const auto& equipment : game::sprinkler_equipment()) result.push_back(equipment.name);
    return result;
}
const std::vector<std::string> facings{"0°","45° right","90° right","135° right","180°","135° left","90° left","45° left"};
const std::vector<std::string> return_methods{"Walk","Reset"};
bool boolean(const Settings& settings,const std::string& key) {
    const auto it=settings.find(key);
    if (it==settings.end() || !std::holds_alternative<bool>(it->second)) throw std::invalid_argument("Invalid gathering setting: "+key);
    return std::get<bool>(it->second);
}
void pattern_settings(std::vector<extensions::Setting>& settings,const std::string& prefix,const FieldDefaults* defaults=nullptr) {
    settings.push_back({prefix+"pattern-scale","Size (%)",defaults?defaults->scale:100,25,200,{}});
    settings.push_back({prefix+"pattern-repetitions","Reps",1,1,9,{}});
    settings.push_back({prefix+"invert-fb","Invert F/B",false});
    settings.push_back({prefix+"invert-lr","Invert L/R",defaults && defaults->invert_lr});
}
void position_settings(std::vector<extensions::Setting>& settings,const std::string& prefix,Point offset={}) {
    settings.push_back({prefix+"start-forward","Forward / back",int(std::round(offset.y*10)),-200,200,{}});
    settings.push_back({prefix+"start-side","Right / left",int(std::round(offset.x*10)),-200,200,{}});
}
std::string phase_name(Phase phase) {
    switch (phase) {
    case Phase::waiting: return "Waiting to start";
    case Phase::travelling: return "Travelling to field";
    case Phase::preparing: return "Preparing field";
    case Phase::gathering: return "Gathering";
    case Phase::returning: return "Returning to hive";
    case Phase::converting: return "Converting";
    case Phase::finished: return "Completed";
    case Phase::recovering: return "Recovering at hive";
    }
    throw std::logic_error("Invalid gathering phase");
}
class GatheringModule final : public extensions::Module {
    std::string id_;
    Plan defaults_,plan_;
    std::shared_ptr<const game::RouteNetwork> network_;
    std::shared_ptr<const game::FieldPolicy> policy_;
    std::shared_ptr<const Source> source_;
    std::optional<PatternControls> controls_;
    std::optional<SprinklerControls> sprinklers_;
    mutable std::shared_ptr<const Results> results_;
    mutable std::string detail_;
    bool started_{};
    std::vector<extensions::HotbarUse> hotbar_uses_;
    mutable bool routine_run_{};
    mutable std::size_t rotation_{};
public:
    GatheringModule(const extensions::Manifest& manifest,Plan defaults,std::shared_ptr<const game::RouteNetwork> network,
        std::shared_ptr<const game::FieldPolicy> policy,std::shared_ptr<const Source> source,std::optional<PatternControls> controls,std::optional<SprinklerControls> sprinklers)
        :id_(manifest.id),defaults_(std::move(defaults)),network_(std::move(network)),policy_(std::move(policy)),source_(std::move(source)),controls_(std::move(controls)),sprinklers_(std::move(sprinklers)) {
        const auto expected=module_manifest(defaults_,id_,manifest.provider,controls_,sprinklers_);
        if (id_.empty() || manifest.permissions!=permissions || !manifest.requests.empty() || !manifest.scripts.empty()
            || !manifest.sources.empty() || !manifest.bindings.empty() || manifest.settings.size()!=expected.settings.size())
            throw std::invalid_argument("Incompatible gathering module manifest");
        std::set<std::string> seen;
        for (const auto& setting : manifest.settings) {
            const auto original=std::find_if(expected.settings.begin(),expected.settings.end(),[&](const auto& item) { return item.id==setting.id; });
            if (original==expected.settings.end() || !seen.insert(setting.id).second || setting.initial.index()!=original->initial.index()
                || setting.choices!=original->choices || setting.minimum<original->minimum || setting.maximum>original->maximum
                || setting.minimum>setting.maximum)
                throw std::invalid_argument("Incompatible gathering settings schema");
            extensions::validate_setting(setting,setting.initial);
        }
    }
    void start(const Settings& saved) override {
        auto settings=saved; hotbar_uses_.clear();
        const auto schema=module_manifest(defaults_,id_,"native-gathering",controls_,sprinklers_);
        if (controls_ && settings.contains("pattern")) {
            for(const auto& field:schema.settings) if(field.id.starts_with("field-") && !field.id.ends_with("-shift-lock")) settings.try_emplace(field.id,field.initial);
            settings=migrate_settings(2,settings);
        }
        if (controls_ && settings.contains("shift-lock")) {
            for(const auto& field:schema.settings) if(field.id.starts_with("field-") && !field.id.ends_with("-shift-lock")) settings.try_emplace(field.id,field.initial);
            settings=migrate_settings(4,settings);
        }
        for (const auto& setting : schema.settings)
            if (setting.id.starts_with("field-") || setting.id.starts_with("start-") || setting.id=="whirligig-slot" || setting.id=="use-whirligig" || setting.id=="shift-lock" || setting.id=="return-method" || setting.id=="recenter" || setting.id=="pattern-scale" || setting.id=="pattern-repetitions" || setting.id=="invert-fb" || setting.id=="invert-lr" || (!controls_ && setting.id=="fields")) settings.try_emplace(setting.id,setting.initial);
        extensions::validate_settings(schema,settings);
        auto plan=defaults_; plan.cycles=std::get<int>(settings.at("cycles")); plan.config.duration=Time(std::get<int>(settings.at("run-seconds"))*1000);
        if (const auto found=settings.find("fields"); found!=settings.end()) {
            const auto* selected=std::get_if<SettingSelection>(&found->second);
            if (!selected || selected->empty()) throw std::invalid_argument("Select at least one gathering field");
            std::set<std::string> selected_names(selected->begin(),selected->end());
            std::vector<FieldPlan> ordered;
            ordered.reserve(plan.fields.size());
            for (const auto& name : *selected) {
                const auto field=std::find_if(plan.fields.begin(),plan.fields.end(),[&](const auto& field) { return field.preference.field==name; });
                if (field==plan.fields.end()) throw std::invalid_argument("Unknown gathering field");
                ordered.push_back(*field); ordered.back().preference.enabled=true;
            }
            for (auto& field : plan.fields) if (!selected_names.contains(field.preference.field)) {
                field.preference.enabled=false; ordered.push_back(std::move(field));
            }
            plan.fields=std::move(ordered);
        }
        if (controls_) {
            struct Choice { automation::Pattern pattern; automation::ProgramFactory strategy; };
            const auto generate=[&](const std::string& prefix,unsigned heading) {
                const auto found=settings.find(prefix+"pattern");
                const auto* name=found==settings.end()?nullptr:std::get_if<std::string>(&found->second);
                const auto preset=std::find_if(controls_->presets.begin(),controls_->presets.end(),[&](const auto& item) { return name && item.name==*name; });
                if (preset==controls_->presets.end()) throw std::invalid_argument("Select an available gathering pattern");
                const auto width=std::get<int>(settings.at(prefix+"pattern-width"));
                const auto duration=std::chrono::minutes(std::get<int>(settings.at(prefix+"gather-minutes")));
                const auto limit=std::get<int>(settings.at(prefix+"backpack-percent"));
                Choice result{{{},duration,double(limit),{},"Gathering"},{}};
                const Settings parameters{{"width",width},{"scale",std::get<int>(settings.at(prefix+"pattern-scale"))},
                    {"repetitions",std::get<int>(settings.at(prefix+"pattern-repetitions"))},
                    {"invert-fb",boolean(settings,prefix+"invert-fb")},{"invert-lr",boolean(settings,prefix+"invert-lr")},{"facing",int(heading)}};
                if (preset->configured) result.strategy=preset->configured(parameters);
                else if (preset->generate) {
                    auto legs=preset->generate(width);
                    const auto flip=[&](Key key) {
                        if (boolean(parameters,"invert-fb")) { if (key==Key::forward) return Key::backward; if (key==Key::backward) return Key::forward; }
                        if (boolean(parameters,"invert-lr")) { if (key==Key::left) return Key::right; if (key==Key::right) return Key::left; }
                        return key;
                    };
                    for (auto& step:legs) { step.leg.tiles*=std::get<int>(parameters.at("scale"))/100.; step.leg.first=flip(step.leg.first); if (step.leg.second) step.leg.second=flip(*step.leg.second); }
                    for (int i=0;i<std::get<int>(parameters.at("repetitions"));++i) result.pattern.legs.insert(result.pattern.legs.end(),legs.begin(),legs.end());
                }
                else {
                    result.strategy=preset->strategy(width);
                }
                if (!preset->generate && !result.strategy) throw std::invalid_argument("Pattern provider returned no strategy");
                return result;
            };
            for (auto& field : plan.fields) {
                const auto prefix=field_prefix(field.preference.field)+"-";
                const auto& facing=std::get<std::string>(settings.at(prefix+"facing"));
                const auto heading=static_cast<unsigned>(std::find(facings.begin(),facings.end(),facing)-facings.begin());
                auto rotation=game::heading_move(0,heading);
                if (rotation && field.rotation) { rotation->press=field.rotation->press; rotation->release=field.rotation->release; }
                field.rotation=rotation;
                field.shift_lock=boolean(settings,prefix+"shift-lock");
                field.extend_while=std::get<SettingSelection>(settings.at(prefix+"extend-while"));
                field.extension_limit=std::chrono::minutes(std::get<int>(settings.at(prefix+"extend-minutes")));
                auto chosen=generate(prefix,heading);
                field.pattern=std::move(chosen.pattern); field.strategy=std::move(chosen.strategy);
            }
        }
        if (!controls_) plan.shift_lock=boolean(settings,"shift-lock");
        std::string equipment;
        unsigned slot=1;
        if (sprinklers_ && sprinklers_->equipment) { equipment=sprinklers_->equipment->equipment; slot=sprinklers_->equipment->slot; }
        else if (sprinklers_) {
            equipment=std::get<std::string>(settings.at("sprinkler-type")); slot=std::get<int>(settings.at("sprinkler-slot"));
            if (equipment!="None") hotbar_uses_.push_back({slot,"Sprinkler","Sprinkler"});
        }
        unsigned return_slot=0;
        if (plan.whirligig_return && boolean(settings,"use-whirligig")) return_slot=std::get<int>(settings.at("whirligig-slot"));
        for (auto& field:plan.fields) {
            const auto field_key=field_prefix(field.preference.field)+"-";
            const auto prefix=controls_?field_key:std::string{};
            field.start_offset={std::get<int>(settings.at(prefix+"start-side"))/10.,std::get<int>(settings.at(prefix+"start-forward"))/10.};
            if (plan.route_recovery) {
                field.reset_return=std::get<std::string>(settings.at(prefix+"return-method"))=="Reset";
                if (field.reset_return) field.return_setup=plan.route_recovery;
            }
            if (return_slot) {
                field.return_item=[create=plan.whirligig_return,slot=return_slot](automation::ProgramFactory return_method) { return create(slot,std::move(return_method)); };
                if (field.preference.enabled && std::none_of(hotbar_uses_.begin(),hotbar_uses_.end(),[](const auto& use) { return use.item=="Whirligig"; }))
                    hotbar_uses_.push_back({return_slot,"Whirligig","Return to hive"});
            }
            if (sprinklers_) {
                field.prepare=[prepare=sprinklers_->prepare,configured=field,equipment,slot] { return prepare(configured,equipment,slot); };
                if (sprinklers_->recenter)
                    field.pattern.between_cycles=boolean(settings,prefix+"recenter") && equipment=="Supreme"?sprinklers_->recenter:automation::ProgramFactory{};
            }
        }
        gathering::program(plan,network_,policy_,source_);
        plan_=std::move(plan); results_.reset(); detail_.clear(); routine_run_=false; rotation_=0; started_=true;
    }
    void update(const extensions::SessionView& view) override {
        if (!started_) throw std::logic_error("Gathering module is not started");
        detail_=view.feature && view.feature->module_id==id_?view.feature->message:std::string{};
    }
    std::vector<extensions::HotbarUse> hotbar_uses() const override { return hotbar_uses_; }
    std::vector<extensions::Metric> metrics() const override {
        if (!started_) return {{"Status","Stopped"}};
        if (!results_) return {{"Status","Ready"},{"Fields",std::to_string(std::count_if(plan_.fields.begin(),plan_.fields.end(),[](const auto& f) { return f.preference.enabled; }))},
            {"Completed cycles","0 / "+std::to_string(plan_.cycles)}};
        const auto report=results_->read();
        auto status=phase_name(report.phase);
        if (report.state==automation::State::failed) status="Stopped after an error";
        else if (report.state==automation::State::deferred) status="Deferred until the next check";
        else if (report.state==automation::State::paused) status="Paused";
        else if (report.state==automation::State::stopped) status="Stopped";
        std::string reason;
        switch (report.reason) {
        case game::FieldReason::goal: reason="Active field request"; break;
        case game::FieldReason::protected_field: reason="Protected field"; break;
        case game::FieldReason::fallback: reason="Saved field rotation"; break;
        case game::FieldReason::no_field: reason=report.state==automation::State::ready?"Not selected":"No eligible field"; break;
        case game::FieldReason::unknown_location: reason="Waiting for location"; break;
        case game::FieldReason::protected_unavailable: reason="Protected field unavailable"; break;
        }
        std::vector<extensions::Metric> result{{"Status",status},{"Field",report.field.value_or("Not selected")},{"Selection",reason},
            {"Completed cycles",std::to_string(report.completed_cycles)+" / "+std::to_string(report.target_cycles)}};
        std::string owners;
        std::set<std::string> seen;
        for (const auto& contributor : report.contributors) seen.insert(contributor.owner);
        std::size_t shown=0;
        for (const auto& owner : seen) {
            if (shown++==3) break;
            if (!owners.empty()) owners+=", "; owners+=owner;
        }
        if (seen.size()>3) owners+=" + "+std::to_string(seen.size()-3)+" more";
        if (!owners.empty()) result.push_back({"Requested by",owners});
        if (report.state!=automation::State::ready && !detail_.empty()) result.push_back({"Current action",detail_});
        return result;
    }
    std::optional<std::vector<extensions::Metric>> report_metrics() const override {
        if (!results_) return std::vector<extensions::Metric>{{"Status",started_?"Ready":"Stopped"}};
        const auto report=results_->read();
        return std::vector<extensions::Metric>{{"Field",report.field.value_or("Not selected")},
            {"Cycles this visit",std::to_string(report.completed_cycles)}};
    }
    void stop() noexcept override { results_.reset(); detail_.clear(); started_=false; }
    std::optional<extensions::RoutineParticipation> routine() const override { return extensions::RoutineParticipation{0,Time(1000),true}; }
    std::unique_ptr<automation::Program> routine_program() const override {
        if (!started_) throw std::logic_error("Enable Gathering before starting it");
        if (routine_run_ && results_) {
            const auto previous=results_->read();
            if (previous.state==automation::State::completed && previous.completed_cycles && previous.reason==game::FieldReason::fallback) {
                for (std::size_t i=0;i<plan_.fields.size();++i)
                    if (plan_.fields[i].preference.field==previous.field) rotation_=(i+1)%plan_.fields.size();
            }
        }
        auto plan=plan_; plan.cycles=1;
        std::rotate(plan.fields.begin(),plan.fields.begin()+rotation_,plan.fields.end());
        auto next=run(std::move(plan),network_,policy_,source_);
        results_=std::move(next.results); detail_.clear(); routine_run_=true; return std::move(next.program);
    }
    bool executable() const noexcept override { return true; }
    std::unique_ptr<automation::Program> program() const override {
        if (!started_) throw std::logic_error("Enable Gathering before starting it");
        routine_run_=false; auto next=run(plan_,network_,policy_,source_); results_=std::move(next.results); detail_.clear(); return std::move(next.program);
    }
};
}
Settings migrate_settings(int version,const Settings& saved) {
    if (version<1 || version>8) throw std::invalid_argument("Unsupported gathering settings version");
    auto settings=saved;
    std::erase_if(settings,[](const auto& entry) {
        const auto& key=entry.first;
        return key=="sprinkler-spacing-forward" || key=="sprinkler-spacing-lateral"
            || (key.starts_with("field-") && (key.ends_with("-sprinkler-spacing-forward") || key.ends_with("-sprinkler-spacing-lateral")));
    });
    for(const auto* key:{"convert-balloon","balloon-mode","balloon-interval","balloon-minutes"}) settings.erase(key);
    if (settings.contains("pattern")) {
        std::vector<std::string> prefixes;
        for(const auto& [key,value]:settings) if(key.starts_with("field-") && key.ends_with("-pattern")) prefixes.push_back(key.substr(0,key.size()-7));
        if(settings.contains("fields")) for(const auto& field:std::get<SettingSelection>(settings.at("fields"))) {
            const auto prefix=field_prefix(field)+"-";
            if(std::find(prefixes.begin(),prefixes.end(),prefix)==prefixes.end()) prefixes.push_back(prefix);
        }
        const std::vector<std::string> pattern_keys{"pattern","pattern-width","gather-minutes","backpack-percent","pattern-scale","pattern-repetitions","invert-fb","invert-lr"};
        const std::vector<std::string> layout_keys{"start-forward","start-side","return-method","recenter"};
        for(const auto& prefix:prefixes) {
            for(const auto& key:pattern_keys) if((!settings.contains(prefix+"override") || !boolean(settings,prefix+"override")) && settings.contains(key)) settings[prefix+key]=settings.at(key);
            for(const auto& key:layout_keys) if((!settings.contains(prefix+"layout-override") || !boolean(settings,prefix+"layout-override")) && settings.contains(key)) settings[prefix+key]=settings.at(key);
            if(!settings.contains(prefix+"override") || !boolean(settings,prefix+"override")) settings.erase(prefix+"facing");
            settings.erase(prefix+"override"); settings.erase(prefix+"layout-override");
        }
        for(const auto& key:pattern_keys) settings.erase(key);
        for(const auto& key:layout_keys) settings.erase(key);
    }
    const std::vector<std::string> old_facings{"No turn","Right ×1","Right ×2","Right ×3","Right ×4","Left ×3","Left ×2","Left ×1"};
    std::vector<std::string> prefixes;
    for(auto& [key,value]:settings) {
        if(key.starts_with("field-") && key.ends_with("-pattern")) prefixes.push_back(key.substr(0,key.size()-7));
        if(key.ends_with("-facing")) if(auto* facing=std::get_if<std::string>(&value)) {
            const auto found=std::find(old_facings.begin(),old_facings.end(),*facing);
            if(found!=old_facings.end()) *facing=facings[found-old_facings.begin()];
        }
    }
    if(settings.contains("fields")) for(const auto& field:std::get<SettingSelection>(settings.at("fields"))) prefixes.push_back(field_prefix(field)+"-");
    if(!prefixes.empty() && settings.contains("shift-lock")) {
        for(const auto& prefix:prefixes) settings.try_emplace(prefix+"shift-lock",settings.at("shift-lock"));
        settings.erase("shift-lock");
    }
    if (version==6) for (const auto& field:field_defaults) {
        const auto prefix=field_prefix(std::string(field.name))+"-";
        const auto unchanged=[&](const std::string& key,const SettingValue& value) {
            const auto found=settings.find(prefix+key);
            return found!=settings.end() && found->second==value;
        };
        if (!unchanged("pattern",std::string(field.pattern)) || !unchanged("pattern-width",field.width)
            || !unchanged("start-forward",0) || !unchanged("start-side",0)
            || !unchanged("facing",std::string("0°")) || !unchanged("invert-fb",false) || !unchanged("invert-lr",false)) continue;
        settings[prefix+"start-forward"]=int(std::round(field.start.y*10));
        settings[prefix+"start-side"]=int(std::round(field.start.x*10));
        settings[prefix+"facing"]=facings.at(field.heading);
        settings[prefix+"invert-lr"]=field.invert_lr;
        if (unchanged("pattern-scale",100)) settings[prefix+"pattern-scale"]=field.scale;
    }
    bool used_whirligig=false;
    for (auto& [key,value]:settings) if (key=="return-method" || key.ends_with("-return-method")) {
        if (auto* method=std::get_if<std::string>(&value); method && *method=="Whirligig / reset") { *method="Reset"; used_whirligig=true; }
    }
    if (const auto found=settings.find("whirligig-slot");found!=settings.end()) if (const auto* old=std::get_if<std::string>(&found->second)) {
        if (*old!="Off" && (old->size()!=1 || old->front()<'1' || old->front()>'7')) throw std::invalid_argument("Invalid saved Whirligig slot");
        const bool configured=*old!="Off";
        const int slot=configured?old->front()-'0':1;
        found->second=slot; settings["use-whirligig"]=configured && used_whirligig;
    }
    return settings;
}
extensions::Manifest module_manifest(const Plan& defaults,std::string id,std::string provider,const std::optional<PatternControls>& controls,const std::optional<SprinklerControls>& sprinklers) {
    if (defaults.cycles<1 || defaults.cycles>10000 || defaults.config.duration<Time(1000)
        || defaults.config.duration>std::chrono::hours(8) || defaults.config.duration.count()%1000)
        throw std::invalid_argument("Invalid gathering module defaults");
    extensions::Manifest result;
    result.id=std::move(id); result.provider=std::move(provider); result.name="Gathering"; result.version="0.1.0";
    result.enabled_by_default=true;
    result.setup_profiles={{"Honey farming",50,{},true,{},"Gather 15 min per field or until 95% bag. Convert, collect due rewards, then resume."},
        {"Quest progress",50,{},true,{},"5 min per field; return at 90% bag; yield to quest work."},
        {"Materials",50,{},true,{},"5 min per field; return at 90% bag; gather between item trips."}};
    result.description="Follow the configured field rotation, gather, return to the hive and convert. Active field requests can temporarily change the selected field.";
    result.consumes.insert(extensions::RequestService::fields);
    result.permissions=permissions; result.settings_version=9;
    result.settings={{"cycles","Completed cycles",static_cast<int>(defaults.cycles),1,10000,{}},
        {"run-seconds","Run limit (seconds)",static_cast<int>(defaults.config.duration.count()/1000),1,28800,{}}};
    if (!defaults.fields.empty()) {
        SettingSelection selected; std::vector<std::string> choices;
        for (const auto& field : defaults.fields) {
            choices.push_back(field.preference.field);
            if (field.preference.enabled) selected.push_back(field.preference.field);
        }
        result.settings.push_back({"fields","Fields",selected,0,0,choices,true});
    }
    if (!controls) result.settings.push_back({"shift-lock","Shift Lock",defaults.shift_lock});
    if (!controls) {
        position_settings(result.settings,"");
        if (defaults.route_recovery) result.settings.push_back({"return-method","Return",std::string("Walk"),0,0,return_methods});
    }
    if (sprinklers) {
        if (!sprinklers->prepare) throw std::invalid_argument("Sprinkler controls need a setup factory");
        if (!sprinklers->equipment) {
            result.settings.push_back({"sprinkler-type","Sprinklers",std::string("None"),0,0,sprinkler_choices()});
            result.settings.push_back({"sprinkler-slot","Sprinkler Builder hotbar slot",1,1,7,{}});
        }
        if (sprinklers->recenter && !controls) result.settings.push_back({"recenter","FDC",false});
    }
    result.page=extensions::PageLayout{{},true};
    if (!defaults.fields.empty()) result.page->sections.push_back({"fields","Field rotation","Select fields and arrange the order to visit them.",{"fields"},false,1});
    if (defaults.whirligig_return) {
        result.settings.push_back({"use-whirligig","Use Whirligig",false});
        result.settings.back().help="Try Whirligig before returning from any gathering field. Otherwise use that field's Walk or Reset choice.";
        result.settings.push_back({"whirligig-slot","Slot",1,1,7,{}});
        result.settings.back().enabled_by="use-whirligig";
        result.settings.back().help="The hotbar slot containing Whirligigs.";
        result.page->sections.push_back({"return-item","Use Whirligig",{},{"use-whirligig","whirligig-slot"},false,2});
    }
    if (sprinklers && !sprinklers->equipment) result.page->sections.push_back({"sprinklers","Sprinklers",{},
        {"sprinkler-type","sprinkler-slot"},false,2,true});
    if (sprinklers && sprinklers->recenter && !controls) result.page->sections.push_back({"correction","Field correction",{},{"recenter"},false,1});
    if (!controls) result.page->sections.push_back({"position","Field starting position","Distance from route arrival, in tiles.",
        {"start-forward","start-side"},false,2,true});
    result.page->sections.push_back({"run","Gathering",{},{"cycles","run-seconds"},false,2});
    if (!controls) result.page->sections.back().settings.push_back("shift-lock");
    if (defaults.route_recovery && !controls) result.page->sections.back().settings.push_back("return-method");
    if (controls) {
        std::set<std::string> ids;
        const auto names=choices(*controls);
        for (const auto& field : defaults.fields) {
            const auto id=field_prefix(field.preference.field),prefix=id+"-";
            if (!ids.insert(id).second) throw std::invalid_argument("Gathering fields need unique setting identities");
            position_settings(result.settings,prefix,field.start_offset);
            if (defaults.route_recovery) result.settings.push_back({prefix+"return-method","Return",std::string("Walk"),0,0,return_methods});
            if (sprinklers && sprinklers->recenter) result.settings.push_back({prefix+"recenter","FDC",false});
            const auto& name=field.preference.field;
            const auto* field_defaults=defaults_for(name);
            const std::string preferred=field_defaults?std::string(field_defaults->pattern):controls->initial;
            const auto pattern=std::find(names.begin(),names.end(),preferred)!=names.end()?preferred:controls->initial;
            pattern_settings(result.settings,prefix,pattern==preferred?field_defaults:nullptr);
            result.settings.push_back({prefix+"pattern","Pattern",pattern,0,0,names});
            const auto width=pattern==preferred && field_defaults?field_defaults->width:controls->width;
            result.settings.push_back({prefix+"pattern-width","Width",width,1,12,{}});
            result.settings.push_back({prefix+"gather-minutes","Time limit",controls->minutes,1,120,{}});
            result.settings.push_back({prefix+"backpack-percent","Bag limit",controls->backpack,1,100,{}});
            result.settings.push_back({prefix+"extend-while","Finish active buffs",SettingSelection{},0,0,{"Pop Star","Guiding Star","Field boost"}});
            result.settings.back().inline_choices=true;
            result.settings.push_back({prefix+"extend-minutes","Extra time limit",5,1,30,{}});
            const auto heading=field.rotation?game::turned_heading(0,*field.rotation):0;
            result.settings.push_back({prefix+"shift-lock","Shift Lock",field.shift_lock.value_or(defaults.shift_lock)});
            result.settings.push_back({prefix+"facing","Camera turn",facings.at(heading),0,0,facings});
            extensions::PageSection basic{id,"Pattern",{},{prefix+"pattern",prefix+"pattern-width"},false,2};
            basic.collection="fields"; basic.choice=field.preference.field;
            result.page->sections.push_back(std::move(basic));
            extensions::PageSection options{id+"-shape","Pattern options",{},{prefix+"pattern-scale",prefix+"pattern-repetitions",prefix+"invert-fb",prefix+"invert-lr"},false,2,true};
            options.collection="fields"; options.choice=field.preference.field;
            result.page->sections.push_back(std::move(options));
            extensions::PageSection limits{id+"-limits","When to return",{},{prefix+"gather-minutes",prefix+"backpack-percent"},false,2};
            limits.collection="fields"; limits.choice=field.preference.field;
            if (defaults.route_recovery) limits.settings.push_back(prefix+"return-method");
            result.page->sections.push_back(std::move(limits));
            extensions::PageSection extend{id+"-extend","Extend gathering",{},
                {prefix+"extend-while",prefix+"extend-minutes"},false,1,true};
            extend.collection="fields"; extend.choice=field.preference.field;
            result.page->sections.push_back(std::move(extend));
            extensions::PageSection layout{id+"-layout","Position",{},
                {prefix+"facing",prefix+"start-forward",prefix+"start-side",prefix+"shift-lock"},false,2};
            layout.collapsed=true; layout.collection="fields"; layout.choice=field.preference.field;
            if (sprinklers && sprinklers->recenter) layout.settings.push_back(prefix+"recenter");
            result.page->sections.push_back(std::move(layout));
        }
    }
    for (auto& section:result.page->sections) section.inline_labels=true;
    for(auto& setting:result.settings) {
        if(setting.id.ends_with("gather-minutes")) setting.suffix=" min";
        if(setting.id.ends_with("extend-minutes")) setting.suffix=" min";
        if(setting.id.ends_with("extend-while")) setting.help="Stay past the time limit while a selected buff is active. Bag limits, completed objectives and gather interrupts still end the run.";
        if(setting.id.ends_with("backpack-percent")) setting.suffix="%";
        if(setting.id=="run-seconds" || setting.id=="cycles") setting.hidden=true;
        if(setting.id.ends_with("recenter")) setting.help="Field drift correction using a Supreme sprinkler.";
        if(setting.id.ends_with("start-side") || setting.id.ends_with("start-forward")) {
            setting.decimals=1; setting.suffix=" tiles";
            setting.help=setting.id.ends_with("start-forward")?"From route arrival, before turning. Positive moves forward; negative moves back.":"From route arrival, before turning. Positive moves right; negative moves left.";
        }
        if(setting.id.ends_with("facing")) setting.help="Camera rotation after placing sprinklers. Each key press turns 45°.";
        if(setting.id.ends_with("pattern-scale")) setting.help="Movement size passed to Lua as scale. 100% is the pattern's normal size.";
        if(setting.id.ends_with("pattern-width")) setting.help="Passed to Lua as width; the pattern controls how it uses this value.";
        if(setting.id.ends_with("pattern-repetitions")) setting.help="Passed to Lua as repetitions. Its effect depends on the pattern.";
    }
    for (auto& profile:result.setup_profiles) for (const auto& setting:result.settings) {
        const bool honey=profile.name=="Honey farming";
        if (setting.id.ends_with("gather-minutes")) profile.settings[setting.id]=honey?15:5;
        if (setting.id.ends_with("backpack-percent")) profile.settings[setting.id]=honey?95:90;
        if (setting.id=="fields" && !setting.choices.empty()) {
            const auto sunflower=std::find(setting.choices.begin(),setting.choices.end(),"Sunflower");
            profile.if_empty[setting.id]=SettingSelection{sunflower==setting.choices.end()?setting.choices.front():*sunflower};
        }
    }
    return result;
}
extensions::Factory module_factory(Plan defaults,automation::NavigationServices navigation,
    std::shared_ptr<const game::FieldPolicy> policy,std::shared_ptr<const game::FieldGoalSource> goals,std::optional<PatternControls> controls,std::optional<SprinklerControls> sprinklers) {
    if (!navigation.routes || !navigation.perception || !policy || !goals) throw std::invalid_argument("Gathering needs navigation, field policy and field goals");
    auto network=std::make_shared<const game::RouteNetwork>(navigation.routes->network());
    auto source=std::make_shared<NavigationSource>(std::move(navigation.perception),std::move(goals));
    module_manifest(defaults,"hive.gathering","native-gathering",controls,sprinklers); gathering::program(defaults,network,policy,source);
    return [defaults=std::move(defaults),network=std::move(network),policy=std::move(policy),source=std::move(source),controls=std::move(controls),sprinklers=std::move(sprinklers)](const extensions::Manifest& manifest) {
        return std::make_unique<GatheringModule>(manifest,defaults,network,policy,source,controls,sprinklers);
    };
}
}
