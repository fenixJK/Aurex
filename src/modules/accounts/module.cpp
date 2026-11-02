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
    std::string follow_mode_,fallback_,last_followed_,boost_field_;
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
        protect_=std::get<bool>(settings.at("protect-support"));
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
            if(running && view.feature && !view.feature->activity_label.empty()) label=view.feature->activity_label;
            state->publish("activity",{{"label",label.substr(0,120)},{"field",view.feature?view.feature->field:std::string{}}});
        } else state->withdraw("activity");
        if(!running || group.role!=coordination::Role::host) {
            for(const auto* topic:{"gathering.field","gathering.boost","game.server","shared.activity","support"}) state->withdraw(topic);
        }
        if(!running) return;
        if(group.role==coordination::Role::host) {
            if(view.feature && view.feature->state==automation::State::running && view.feature->activity==WorkActivity::gathering && known_field(view.feature->field))
                field_=view.feature->field;
            if(!field_.empty()) state->publish("gathering.field",{{"field",field_}});
            // A missing frame must not cancel a confirmed boost or reset its timer.
            if (boosts_) if (const auto sample=boosts_->read();sample && sample->context.generation==view.generation
                && sample->context.now<=view.now && view.now-sample->context.now<=Time(5000) && sample->context.now>reading_at_) {
                reading_at_=sample->context.now;
                if (const auto* detection=dynamic_cast<const game::FieldBoostDetection*>(sample->value.get());detection && detection->readable)
                    for (const auto& boost:detection->boosts) if (known_field(boost.field) && boost.remaining && *boost.remaining>Time{}
                        && *boost.remaining<=game::field_boost_duration)
                        local_boosts_[boost.field]=sample->context.now+*boost.remaining;
            }
            std::erase_if(local_boosts_,[&](const auto& entry) { return entry.second<=view.now; });
            if (const auto boost=local_boosts_.find(field_);boost!=local_boosts_.end())
                state->publish("gathering.boost",{{"field",field_},{"seconds",int((boost->second-view.now).count()/1000)}});
            else state->withdraw("gathering.boost");
            if(!controller_->server().isEmpty()) state->publish("game.server",{{"target",controller_->server().toStdString()},{"epoch",controller_->epoch().toStdString()+"/"+std::to_string(view.generation)}});
            if(view.feature && view.feature->state==automation::State::running && view.feature->shared_activity
                && view.feature->shared_activity->service=="enemies") state->publish("shared.activity",view.feature->shared_activity->target);
            else state->withdraw("shared.activity");
        } else if(group.role==coordination::Role::guest) {
            for(const auto& peer:group.peers) if(peer.host) {
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
                if (follow_mode_=="Boosts only" && boost_until_>view.now) follow_field(boost_field_,view);
                else if (follow_mode_=="Follow main") if(const auto topic=peer.topics.find("gathering.field");topic!=peer.topics.end())
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
        if (group.role!=coordination::Role::host && !main_available && follow_mode_!="Independent"
            && fallback_=="Stay in last field" && !last_followed_.empty()) follow_field(last_followed_,view);
        if (group.role==coordination::Role::guest) {
            if (!following_.empty()) state->publish("support",{{"field",following_}});
            else state->withdraw("support");
        } else state->withdraw("support");
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
        if(connected_) controller_->disable();connected_=false;requests_={};field_.clear();following_.clear();last_followed_.clear();local_boosts_.clear();remote_boost_.clear();boost_until_={};role_=coordination::Role::offline;
    }
};
}
extensions::Manifest module_manifest() {
    extensions::Manifest result;result.id="hive.native-accounts";result.provider="native-accounts";result.presets=false;result.name="Alt Sync";result.settings_version=2;result.version="1.0.0";
    result.description="Share a server, gather together and help with shared fights.";
    result.requests={extensions::RequestService::fields,extensions::RequestService::enemies,extensions::RequestService::servers};
    result.settings={{"follow-mode","Follow",std::string("Follow main"),0,0,{"Follow main","Boosts only","Independent"}},{"interrupt-gather","Gather interrupt",true},
        {"join-host","Join main’s server",false},{"help-fights","Help with shared fights",false}};
    result.settings[2].help="Joins at the next safe workflow boundary and uses the host's target when reconnecting.";
    result.settings[3].help="Requests the main’s fights through Combat, using your own combat settings and cooldowns. Both accounts must be in the same server.";
    result.settings.front().help="Follow the main’s field using your own Gathering settings, or follow only while that field has a confirmed boost. Conversion trips do not change the shared field.";
    result.settings.push_back({"main-unavailable","If main stops or disconnects",std::string("Resume own rotation"),0,0,{"Resume own rotation","Stay in last field"}});
    result.settings.push_back({"protect-support","Keep supporting main",false});
    result.settings.back().help="Defer other modules while following. Allowed modules below still use their own priority and interrupt settings.";
    result.settings.push_back({"allowed-work","Allow while supporting",SettingSelection{"Combat"},0,0,{"Collection","Planters","Quests","Combat","Boosting"}});
    result.settings.back().enabled_by="protect-support";
    result.page=extensions::PageLayout{{{"following","Alt behaviour",{},{"follow-mode","interrupt-gather","join-host","help-fights","main-unavailable"},false,2},
        {"support","While supporting",{},{"protect-support","allowed-work"},false,2}},true};
    return result;
}
Settings migrate_settings(int version,Settings settings) {
    if (version!=1) throw std::invalid_argument("Unknown previous Alt Sync settings version");
    if (const auto old=settings.find("follow-host");old!=settings.end()) {
        settings["follow-mode"]=std::string(std::get<bool>(old->second)?"Follow main":"Independent");settings.erase(old);
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
