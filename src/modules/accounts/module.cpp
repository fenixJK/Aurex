#include "module.hpp"
#include "game/data/fields.hpp"
#include <algorithm>

namespace hive::features::accounts {
namespace {
bool known_field(const std::string& name) {
    const auto fields=game::fields();return std::any_of(fields.begin(),fields.end(),[&](const auto& field) { return field.name==name; });
}
const std::string* text(const Settings& values,const std::string& key) {
    const auto found=values.find(key);return found==values.end()?nullptr:std::get_if<std::string>(&found->second);
}
class Accounts final : public extensions::Module {
    extensions::Manifest manifest_;
    std::shared_ptr<Controller> controller_;
    std::shared_ptr<const game::DetectionSource> boosts_;
    std::string follow_mode_,fallback_,last_followed_,boost_field_,main_peer_;
    QString connection_epoch_;
    Time boost_until_{},reading_at_{};
    std::map<std::string,Time> local_boosts_;
    Settings remote_boost_;
    bool protect_{};
    std::set<std::string> allowed_;
    game::Requests requests_;
    std::string field_,following_;
    bool interrupt_{},join_{},assist_{},connected_{};
    coordination::Role role_{coordination::Role::offline};
    void follow_field(const std::string& field,const extensions::SessionView& view) {
        requests_.fields.push_back({"follow-host",field,85,view.now+Time(3000),{},false,0,interrupt_});following_=field;
    }
public:
    Accounts(extensions::Manifest manifest,std::shared_ptr<Controller> controller,std::shared_ptr<const game::DetectionSource> boosts)
        :manifest_(std::move(manifest)),controller_(std::move(controller)),boosts_(std::move(boosts)) {}
    void start(const Settings& settings) override {
        extensions::validate_settings(manifest_,settings);
        follow_mode_=std::get<std::string>(settings.at("follow-mode"));
        fallback_=std::get<std::string>(settings.at("main-unavailable"));
        protect_=std::get<std::string>(settings.at("other-tasks"))=="Only selected tasks";
        const std::map<std::string,std::string> modules{{"Collection","hive.native-collection"},{"Planters","hive.native-planters"},
            {"Quests","hive.native-quests"},{"Combat","hive.native-combat"},{"Boosting","hive.native-boosting"}};
        allowed_.clear();
        for (const auto& name:std::get<SettingSelection>(settings.at("allowed-work"))) allowed_.insert(modules.at(name));
        interrupt_=std::get<bool>(settings.at("interrupt-gather"));
        join_=std::get<bool>(settings.at("join-host"));assist_=std::get<bool>(settings.at("help-fights"));
    }
    void update(const extensions::SessionView& view) override {
        if(!connected_) { controller_->enable();connected_=true; }
        const auto& state=controller_->state();const auto group=state->snapshot();
        if(connection_epoch_!=controller_->epoch() || requests_.generation!=view.generation || view.status.state!=RunState::running) {
            field_.clear();last_followed_.clear();local_boosts_.clear();remote_boost_.clear();boost_until_={};reading_at_={};
            main_peer_.clear();
        }
        if (group.role!=role_ && group.role!=coordination::Role::offline) {
            field_.clear();last_followed_.clear();remote_boost_.clear();boost_until_={};
        }
        connection_epoch_=controller_->epoch();
        role_=group.role;requests_.generation=view.generation;++requests_.revision;
        requests_.fields.clear();requests_.enemies.clear();requests_.channels.clear();following_.clear();
        const bool running=view.status.state==RunState::running;
        if(group.role!=coordination::Role::offline) {
            auto label=extensions::run_state_name(view.status.state);
            if(running && view.feature) {
                if (!view.feature->status.empty()) label=view.feature->status.title;
                else if (!view.feature->activity_label.empty()) label=view.feature->activity_label;
            }
            state->publish("activity",{{"label",label.substr(0,120)},{"field",running && view.feature?view.feature->field:std::string{}}});
        } else state->withdraw("activity");
        if(!running || group.role!=coordination::Role::host) {
            for(const auto* topic:{"gathering.field","gathering.boost","game.server","shared.activity"}) state->withdraw(topic);
        }
        if(!running) { state->withdraw("support");controller_->main_boost()->update({});return; }
        std::optional<coordination::MainBoost> main_boost;
        if(group.role==coordination::Role::host) {
            if(view.feature && view.feature->state==automation::State::running && view.feature->activity==WorkActivity::gathering && known_field(view.feature->field))
                field_=view.feature->field;
            if(!field_.empty()) state->publish("gathering.field",{{"field",field_}});
            // A missing frame must not cancel a confirmed boost or reset its timer.
            if (boosts_) if (const auto sample=boosts_->read();sample && sample->error.empty() && sample->context.generation==view.generation
                && sample->context.now<=view.now && view.now-sample->context.now<=Time(5000) && sample->context.now>reading_at_) {
                reading_at_=sample->context.now;
                if (const auto* detection=dynamic_cast<const game::FieldBoostDetection*>(sample->value.get());detection && detection->readable)
                    for (const auto& boost:detection->boosts) if (known_field(boost.field) && boost.remaining && *boost.remaining>Time{}
                        && *boost.remaining<=game::field_boost_duration)
                        local_boosts_[boost.field]=sample->context.now+*boost.remaining;
            }
            std::erase_if(local_boosts_,[&](const auto& entry) { return entry.second<=view.now; });
            if (const auto boost=local_boosts_.find(field_);boost!=local_boosts_.end()) {
                main_boost=coordination::MainBoost{field_,boost->second,view.now,view.generation,false};
                state->publish("gathering.boost",{{"field",field_},{"seconds",int((boost->second-view.now).count()/1000)},
                    {"session",std::to_string(view.session_id)+"/"+std::to_string(view.generation)}});
            }
            else state->withdraw("gathering.boost");
            if(!controller_->server().isEmpty()) state->publish("game.server",{{"target",controller_->server().toStdString()},{"epoch",controller_->epoch().toStdString()+"/"+std::to_string(view.generation)}});
            if(view.feature && view.feature->state==automation::State::running && view.feature->shared_activity
                && view.feature->shared_activity->service=="enemies") state->publish("shared.activity",view.feature->shared_activity->target);
            else state->withdraw("shared.activity");
        } else if(group.role==coordination::Role::guest) {
            for(const auto& peer:group.peers) if(peer.host) {
                if (main_peer_!=peer.id) { main_peer_=peer.id;remote_boost_.clear();boost_until_={}; }
                if(join_) if(const auto topic=peer.topics.find("game.server");topic!=peer.topics.end()) {
                    const auto target=text(topic->second,"target"),epoch=text(topic->second,"epoch");
                    if(target && epoch && game::request_identifier(peer.id+"/"+*epoch)) {
                        try {
                            game::JoinTarget::from_uri(*target);
                            requests_.channels[extensions::RequestService::servers].push_back({"host-server",90,view.now+Time(3000),
                                {{"target",*target},{"epoch",peer.id+"/"+*epoch},{"interrupt",interrupt_}}});
                        } catch(const std::invalid_argument&) {}
                    }
                }
                const auto boost=peer.topics.find("gathering.boost");
                if (boost==peer.topics.end()) { remote_boost_.clear();boost_until_={}; }
                else if (boost->second!=remote_boost_) {
                    remote_boost_=boost->second;boost_until_={};
                    const auto field=text(remote_boost_,"field");const auto seconds=remote_boost_.find("seconds");
                    if (field && known_field(*field) && seconds!=remote_boost_.end())
                        if (const auto remaining=std::get_if<int>(&seconds->second);remaining && *remaining>0 && *remaining<=std::chrono::duration_cast<std::chrono::seconds>(game::field_boost_duration).count()) {
                            boost_field_=*field;boost_until_=view.now+std::chrono::seconds(*remaining);
                        }
                }
                if (boost_until_>view.now) main_boost=coordination::MainBoost{boost_field_,boost_until_,view.now,view.generation,false};
                if (follow_mode_=="Main's boosted field" && main_boost) follow_field(main_boost->field,view);
                else if (follow_mode_=="Main's field") if(const auto topic=peer.topics.find("gathering.field");topic!=peer.topics.end())
                    if(const auto field=text(topic->second,"field");field && known_field(*field)) follow_field(*field,view);
                if (!following_.empty()) last_followed_=following_;
                if(assist_) if(const auto topic=peer.topics.find("shared.activity");topic!=peer.topics.end()) {
                    const auto enemy=text(topic->second,"enemy"),field=text(topic->second,"field");
                    if(enemy && game::request_identifier(*enemy) && field && game::field_at(*field+".entry"))
                        requests_.enemies.push_back({"host-fight",*enemy,85,view.now+Time(3000),*field});
                }
                break;
            }
        }
        const bool main_available=std::any_of(group.peers.begin(),group.peers.end(),[](const auto& peer) {
            return peer.host && peer.topics.contains("gathering.field");
        });
        if (group.role!=coordination::Role::host && !main_available && follow_mode_!="Own field rotation"
            && fallback_=="Stay in last field" && !last_followed_.empty()) follow_field(last_followed_,view);
        if (group.role==coordination::Role::guest) {
            if (!following_.empty()) state->publish("support",{{"field",following_}});
            else state->withdraw("support");
        } else state->withdraw("support");
        if (main_boost) main_boost->supporting=group.role==coordination::Role::guest && following_==main_boost->field;
        controller_->main_boost()->update(std::move(main_boost));
    }
    std::optional<extensions::WorkWindow> protected_work(const extensions::SessionView& view) const override {
        if (!protect_ || following_.empty() || view.status.state!=RunState::running) return {};
        return extensions::WorkWindow{"Supporting main in "+following_,view.now+Time(3000),allowed_,assist_,false};
    }
    std::vector<extensions::Metric> metrics() const override {
        std::vector<extensions::Metric> result{{"Connection",controller_->status().toStdString()}};
        if(!following_.empty()) result.push_back({"Following",following_});
        if(role_==coordination::Role::host) result.push_back({"Server",controller_->server().isEmpty()?"Not shared":"Shared with group"});
        else if(role_==coordination::Role::guest && join_) result.push_back({"Server",requests_.channels.contains(extensions::RequestService::servers)?"Following host server":"Host has not shared a server"});
        return result;
    }
    game::Requests requests() const override { return requests_; }
    void stop() noexcept override {
        controller_->main_boost()->update({});main_peer_.clear();
        if(connected_) controller_->disable();connected_=false;requests_={};field_.clear();following_.clear();last_followed_.clear();local_boosts_.clear();remote_boost_.clear();boost_until_={};role_=coordination::Role::offline;
    }
};
}
extensions::Manifest module_manifest() {
    extensions::Manifest result;result.id="hive.native-accounts";result.provider="native-accounts";result.presets=false;result.name="Alt Sync";result.settings_version=3;result.version="1.0.0";
    result.description="Choose a main account to lead, then connect alts that follow its field and boosts.";
    result.requests={extensions::RequestService::fields,extensions::RequestService::enemies,extensions::RequestService::servers};
    result.settings={{"follow-mode","Gather in",std::string("Main's field"),0,0,{"Main's field","Main's boosted field","Own field rotation"}},
        {"interrupt-gather","Switch fields immediately",true},{"join-host","Join main's server",false},{"help-fights","Help with main's fights",false}};
    result.settings[0].help="Use your own Gathering pattern in the main's current field, follow only during its confirmed boost, or keep your own field rotation.";
    result.settings[1].help="Switch at the next safe interruption point. When off, finish your current gathering visit first.";
    result.settings[1].enabled_by="follow-mode";result.settings[1].enabled_values={"Main's field","Main's boosted field"};
    result.settings[2].help="Use the main's shared server at the next safe workflow boundary, and when reconnecting.";
    result.settings[3].help="Uses your Combat settings and cooldowns. Both accounts must be in the same server.";
    result.settings.push_back({"main-unavailable","If main is unavailable",std::string("Resume own rotation"),0,0,{"Resume own rotation","Stay in last field"}});
    result.settings.back().enabled_by="follow-mode";result.settings.back().enabled_values={"Main's field","Main's boosted field"};
    result.settings.push_back({"other-tasks","Other tasks",std::string("Run normally"),0,0,{"Run normally","Only selected tasks"}});
    result.settings.back().help="While following, either keep your normal task schedule or allow only the tasks selected below. Conversion still runs.";
    result.settings.push_back({"allowed-work","Tasks allowed while following",SettingSelection{"Combat"},0,0,{"Collection","Planters","Quests","Combat","Boosting"}});
    result.settings.back().enabled_by="other-tasks";result.settings.back().enabled_values={"Only selected tasks"};result.settings.back().inline_choices=true;
    result.page=extensions::PageLayout{{{"following","Gathering",{},{"follow-mode","interrupt-gather","main-unavailable"},false,2},
        {"tasks","Other tasks",{},{"other-tasks"},false,1},
        {"allowed","Tasks allowed while following",{},{"allowed-work"},false,3},
        {"shared","Server & fights",{},{"join-host","help-fights"},false,2}},true};
    for (auto& section:result.page->sections) if(section.id!="allowed") section.inline_labels=true;
    return result;
}
Settings migrate_settings(int version,Settings settings) {
    if (version<1 || version>2) throw std::invalid_argument("Unknown previous Alt Sync settings version");
    if (const auto old=settings.find("follow-host");old!=settings.end()) {
        settings["follow-mode"]=std::string(std::get<bool>(old->second)?"Follow main":"Independent");settings.erase(old);
    }
    if (auto mode=settings.find("follow-mode");mode!=settings.end()) {
        const auto& value=std::get<std::string>(mode->second);
        if (value=="Follow main") mode->second=std::string("Main's field");
        else if (value=="Boosts only") mode->second=std::string("Main's boosted field");
        else if (value=="Independent") mode->second=std::string("Own field rotation");
    }
    if (const auto old=settings.find("protect-support");old!=settings.end()) {
        settings["other-tasks"]=std::string(std::get<bool>(old->second)?"Only selected tasks":"Run normally");settings.erase(old);
    }
    return settings;
}
extensions::Factory module_factory(std::shared_ptr<Controller> controller,std::shared_ptr<const game::DetectionSource> boosts) {
    return [controller=std::move(controller),boosts=std::move(boosts)](const extensions::Manifest& manifest) {
        if(!controller) throw std::invalid_argument("Alt Sync needs its connection service.");
        return std::make_unique<Accounts>(manifest,controller,boosts);
    };
}
}
