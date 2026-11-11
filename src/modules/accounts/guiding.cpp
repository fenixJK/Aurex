#include "guiding.hpp"
#include <algorithm>

namespace hive::features::accounts {
namespace {
template<class T> T value(const Settings& data,const char* key,T fallback={}) {
    const auto it=data.find(key);if(it!=data.end()) if(const auto result=std::get_if<T>(&it->second)) return *result;
    return fallback;
}
const Settings& topic(const coordination::Peer& peer,const char* name) {
    static const Settings empty;
    const auto it=peer.topics.find(name);return it==peer.topics.end()?empty:it->second;
}
}
void GuidingSync::stop() {
    rotation_={};connection_.clear();command_.clear();target_.clear();request_key_.clear();acknowledged_.clear();
    location_.clear();status_.clear();server_epoch_.clear();activation_at_=active_until_=arrived_at_={};
    join_revision_=0;completed_=managed_=away_=desired_main_=false;
    if(servers_) servers_->set_active(false);
}
void GuidingSync::update(Controller& controller,const coordination::Snapshot& group,const extensions::SessionView& view,game::Requests& requests) {
    const auto state=controller.state();
    const auto connection=controller.epoch().toStdString();
    if(connection_!=connection) { stop();connection_=connection; }
    const bool running=view.status.state==RunState::running;
    status_.clear();
    if(group.role==coordination::Role::host) {
        state->withdraw("alt");
        if(!enabled || controller.server().isEmpty()) {
            state->withdraw("guiding.rotation");
            if(enabled && controller.server().isEmpty()) status_="Share a private server or Roblox user ID to rotate Guiding alts";
            return;
        }
        if(!running) return; // Pausing the main must not revoke existing seats.
        std::vector<coordination::RotationMember> members;unsigned unidentified=0;
        for(const auto& peer:group.peers) if(!peer.host) {
            const auto& alt=topic(peer,"alt");const auto id=value<std::string>(alt,"id");
            if(id.empty()) { ++unidentified;continue; }
            members.push_back({id,value<std::string>(alt,"type")=="Guiding",value<bool>(alt,"running"),
                value<bool>(alt,"active"),value<bool>(alt,"complete"),value<std::string>(alt,"epoch")==connection_?value<std::string>(alt,"ack"):std::string{}});
        }
        const auto assignments=rotation_.update(members,slot_limit,reserved+unidentified);
        SettingSelection commands;unsigned admitted=0;
        for(const auto& item:assignments) { commands.push_back(item.id+"/"+item.command);admitted+=item.main; }
        state->publish("guiding.rotation",{{"epoch",connection_},{"commands",commands}});
        status_="Guiding rotation · "+std::to_string(admitted)+" with main · "+std::to_string(assignments.size()-admitted)+" on public servers";
        return;
    }
    state->withdraw("guiding.rotation");
    if(!controller.joined() || controller.role()!=coordination::Role::guest) { state->withdraw("alt");return; }
    const auto account=controller.account_id().toStdString();
    const coordination::Peer* main=nullptr;
    for(const auto& peer:group.peers) if(peer.host) { main=&peer;break; }
    if(view.server.join_revision!=join_revision_) {
        join_revision_=view.server.join_revision;arrived_at_=view.now;active_until_={};activation_at_={};completed_=false;
    }
    if(guiding && running && observations_ && !view.reconnecting) {
        if(const auto reading=observations_->read();reading && reading->error.empty() && reading->context.generation==view.generation
            && reading->context.now<=view.now && view.now-reading->context.now<=Time(3000)) {
            if(const auto detection=dynamic_cast<const game::GuidingDetection*>(reading->value.get());detection && detection->activation) {
                const auto& activation=*detection->activation;
                if(activation.seen>activation_at_ && activation.seen>=arrived_at_ && activation.seen<=view.now) {
                    activation_at_=activation.seen;active_until_=activation.seen+game::guiding_star_duration+Time(2000);
                    completed_=false;
                }
            }
        }
    }
    if(active_until_!=Time{} && view.now>=active_until_ && location_=="main") completed_=true;
    bool valid_command=false;
    if(main && guiding) {
        const auto& rotation=topic(*main,"guiding.rotation");
        const auto epoch=value<std::string>(rotation,"epoch");
        const auto& server=topic(*main,"game.server");
        const auto main_target=value<std::string>(server,"target"),main_epoch=value<std::string>(server,"epoch");
        for(const auto& entry:value<SettingSelection>(rotation,"commands")) {
            const auto prefix=account+"/";
            if(!entry.starts_with(prefix)) continue;
            const auto token=entry.substr(prefix.size());
            if(token.size()<2 || token.size()>24 || (token.back()!='m' && token.back()!='p')
                || !std::all_of(token.begin(),token.end()-1,[](unsigned char c){return c>='0' && c<='9';})
                || !game::request_identifier(epoch) || main_target.empty() || !game::request_identifier(main_epoch)) break;
            try { game::JoinTarget::from_uri(main_target); } catch(const std::invalid_argument&) { break; }
            valid_command=true;managed_=true;desired_main_=token.back()=='m';away_=!desired_main_;
            const auto command=epoch+"/"+token;
            const auto destination_epoch=desired_main_?command+"/"+main_epoch:command;
            if(command_!=command || server_epoch_!=destination_epoch) {
                command_=command;server_epoch_=destination_epoch;target_.clear();request_key_.clear();
                acknowledged_.clear();completed_=false;
            }
            if(desired_main_) target_=main_target;
            break;
        }
    }
    // A transient transport outage does not cancel an in-flight departure or send a waiting alt back to the main.
    if(main && !valid_command && !topic(*main,"guiding.rotation").contains("commands")) {
        managed_=away_=false;command_.clear();target_.clear();request_key_.clear();acknowledged_.clear();
    }
    if(!guiding) { managed_=away_=false; }
    const bool public_search=managed_ && away_ && target_.empty() && running;
    if(servers_) {
        servers_->set_active(public_search);servers_->update(view.now);
        if(public_search) {
            const auto choice=servers_->next(view.now);status_=choice.status;
            if(choice.target && (!main || choice.target->uri()!=value<std::string>(topic(*main,"game.server"),"target")))
                target_=choice.target->uri();
        }
    }
    if(managed_ && running && !target_.empty()) {
        const std::string request_id="guiding-server";
        request_key_=game::server_request_key("hive.native-accounts",request_id,server_epoch_,target_);
        if(view.server.joined_request==request_key_ && !view.reconnecting) {
            acknowledged_=command_.substr(command_.find_last_of('/')+1);location_=desired_main_?"main":"public";
        }
        // Main rejoin sync and rotation both use the normal safe workflow handoff.
        requests.channels[extensions::RequestService::servers].push_back({request_id,95,view.now+Time(3000),
            {{"target",target_},{"epoch",server_epoch_},{"interrupt",true}}});
        status_=desired_main_?(location_=="main" && !acknowledged_.empty()?"With main · waiting for own Guiding Star":"Joining main for Guiding"):
            (!acknowledged_.empty()?"Public server · running normal priorities":"Moving to a public server");
        if(desired_main_ && active_until_>view.now) status_="Holding main's slot · Guiding Star active";
    }
    state->publish("alt",{{"id",account},{"type",std::string(guiding?"Guiding":"Support")},{"running",running},
        {"active",location_=="main" && active_until_>view.now},{"complete",completed_},
        {"ack",acknowledged_},{"epoch",command_.substr(0,command_.find('/'))},{"where",location_},{"status",status_}});
}
}
