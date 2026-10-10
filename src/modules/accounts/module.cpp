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
    game::Requests requests_;
    std::string field_,following_;
    bool follow_{},interrupt_{},join_{},assist_{},connected_{};
    coordination::Role role_{coordination::Role::offline};
    void follow_field(const std::string& field,const extensions::SessionView& view) {
        requests_.fields.push_back({"follow-host",field,85,view.now+Time(3000),{},false,0,interrupt_});following_=field;
    }
public:
    Accounts(extensions::Manifest manifest,std::shared_ptr<Controller> controller):manifest_(std::move(manifest)),controller_(std::move(controller)) {}
    void start(const Settings& settings) override {
        extensions::validate_settings(manifest_,settings);
        follow_=std::get<bool>(settings.at("follow-host"));interrupt_=std::get<bool>(settings.at("interrupt-gather"));
        join_=std::get<bool>(settings.at("join-host"));assist_=std::get<bool>(settings.at("help-fights"));
    }
    void update(const extensions::SessionView& view) override {
        if(!connected_) { controller_->enable();connected_=true; }
        const auto& state=controller_->state();const auto group=state->snapshot();
        if(requests_.generation!=view.generation || role_!=group.role || view.status.state!=RunState::running) field_.clear();
        role_=group.role;requests_.generation=view.generation;++requests_.revision;
        requests_.fields.clear();requests_.enemies.clear();requests_.channels.clear();following_.clear();
        const bool running=view.status.state==RunState::running;
        if(group.role!=coordination::Role::offline) {
            auto label=extensions::run_state_name(view.status.state);
            if(running && view.feature && !view.feature->activity_label.empty()) label=view.feature->activity_label;
            state->publish("activity",{{"label",label.substr(0,120)}});
        } else state->withdraw("activity");
        if(!running || group.role!=coordination::Role::host) {
            for(const auto* topic:{"gathering.field","game.server","shared.activity"}) state->withdraw(topic);
        }
        if(!running) return;
        if(group.role==coordination::Role::host) {
            if(view.feature && view.feature->state==automation::State::running && view.feature->activity==WorkActivity::gathering && known_field(view.feature->field))
                field_=view.feature->field;
            if(!field_.empty()) state->publish("gathering.field",{{"field",field_}});
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
                if(follow_) if(const auto topic=peer.topics.find("gathering.field");topic!=peer.topics.end())
                    if(const auto field=text(topic->second,"field");field && known_field(*field)) follow_field(*field,view);
                if(assist_) if(const auto topic=peer.topics.find("shared.activity");topic!=peer.topics.end()) {
                    const auto enemy=text(topic->second,"enemy"),field=text(topic->second,"field");
                    if(enemy && game::request_identifier(*enemy) && field && game::field_at(*field+".entry"))
                        requests_.enemies.push_back({"host-fight",*enemy,85,view.now+Time(3000),*field});
                }
                break;
            }
        }
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
        if(connected_) controller_->disable();connected_=false;requests_={};field_.clear();following_.clear();role_=coordination::Role::offline;
    }
};
}
extensions::Manifest module_manifest() {
    extensions::Manifest result;result.id="hive.native-accounts";result.provider="native-accounts";result.presets=false;result.name="Accounts";result.version="1.0.0";
    result.description="Share a server, gather together and help with shared fights.";
    result.requests={extensions::RequestService::fields,extensions::RequestService::enemies,extensions::RequestService::servers};
    result.settings={{"follow-host","Follow host field",true},{"interrupt-gather","Gather interrupt",true},
        {"join-host","Join host server",false},{"help-fights","Help with shared fights",false}};
    result.settings[2].help="Joins at the next safe workflow boundary and uses the host's target when reconnecting.";
    result.settings[3].help="Requests shared fights through Combat, using your own combat settings and cooldowns. Accounts must be in the same server.";
    result.settings.front().help="Guests gather where the host gathers, using their own patterns. The host chooses fields in Gathering.";
    result.page=extensions::PageLayout{{{"following","When joining",{},{"follow-host","interrupt-gather","join-host","help-fights"},false,2}},true};
    return result;
}
extensions::Factory module_factory(std::shared_ptr<Controller> controller) {
    return [controller=std::move(controller)](const extensions::Manifest& manifest) {
        if(!controller) throw std::invalid_argument("Accounts needs its connection service.");
        return std::make_unique<Accounts>(manifest,controller);
    };
}
}
