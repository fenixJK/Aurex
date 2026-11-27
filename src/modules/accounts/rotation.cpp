#include "rotation.hpp"
#include <algorithm>
#include <charconv>

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
Time timestamp(const Settings& data,const char* key) {
    const auto text=value<std::string>(data,key);
    Time::rep number{};
    const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),number);
    return error==std::errc{} && end==text.data()+text.size() && number>=0?Time(number):Time(-1);
}
}
void RotationSync::select_job(coordination::AttackJob job) {
    if(job_.id==job.id) return;
    if(visit_) { visit_->allowed=false;visit_->main_ready=false; }
    job_=std::move(job);visit_=std::make_shared<coordination::AttackAssistState>();
    launched_=attack_complete_=false;
}
void RotationSync::stop() {
    rotation_={};connection_.clear();command_.clear();target_.clear();request_key_.clear();acknowledged_.clear();
    location_.clear();status_.clear();server_epoch_.clear();activation_at_=active_until_=arrived_at_={};
    join_revision_=0;completed_=managed_=away_=desired_main_=false;
    if(visit_) { visit_->allowed=false;visit_->main_ready=false; }
    visit_.reset();job_={};scheduled_finished_=Time(-1);attack_pending_=launched_=main_=attack_complete_=false;
    if(servers_) servers_->set_active(false);
}
void RotationSync::update(Controller& controller,const coordination::Snapshot& group,const extensions::SessionView& view,game::Requests& requests) {
    const auto state=controller.state();
    const auto connection=controller.epoch().toStdString();
    if(connection_!=connection) { stop();connection_=connection; }
    const bool running=view.status.state==RunState::running;
    const auto wall=clock_(),hour=Time(std::chrono::hours(1));
    const auto cycle=(wall+std::chrono::minutes(3))/hour*hour;
    attack_pending_=false;main_=group.role==coordination::Role::host;
    status_.clear();
    if(!visit_) visit_=std::make_shared<coordination::AttackAssistState>();
    if(view.server.join_revision!=join_revision_) {
        join_revision_=view.server.join_revision;arrived_at_=view.now;active_until_={};activation_at_={};completed_=false;
        // A completed arrival belongs to that server, never to a later rejoin.
        if(launched_) { visit_->allowed=false;visit_->arrived=false;visit_->finished=true; }
    }
    if(group.role==coordination::Role::host) {
        state->withdraw("alt");
        if(!enabled || controller.server().isEmpty()) {
            visit_->allowed=false;
            state->withdraw("guiding.rotation");
            if(enabled && controller.server().isEmpty()) status_="Share a private server or Roblox user ID to manage alt slots";
            return;
        }
        if(!running) {
            visit_->allowed=false;visit_->main_ready=false;
            // Pausing retains seats, but withdraws permission to act or spend.
            auto published=state->publication().topics;
            if(auto it=published.find("guiding.rotation");it!=published.end()) {
                it->second["attack-ready"]=false;it->second["attack-active"]=false;
                state->publish("guiding.rotation",it->second);
            }
            return;
        }
        coordination::AttackJob requested;
        if(!view.reconnecting && view.feature && view.feature->state==automation::State::running && view.feature->shared_activity
            && view.feature->shared_activity->service=="enemies") {
            const auto& target=view.feature->shared_activity->target;
            const auto enemy=value<std::string>(target,"enemy"),encounter=value<std::string>(target,"encounter");
            if(enemy!="Mondo Chick" && assistance_.destinations.contains(enemy) && game::request_identifier(encounter)) {
                const auto id="fight/"+std::to_string(view.session_id)+"/"+std::to_string(view.generation)+"/"+encounter;
                requested=job_.id==id?job_:coordination::AttackJob{id,enemy,wall,wall+std::chrono::hours(8),false};
            }
        }
        // An actual requested fight takes precedence over the hourly visit.
        const bool window=wall>=cycle-std::chrono::minutes(3) && wall<cycle+std::chrono::minutes(15);
        if(requested.id.empty() && assistance_.destinations.contains("Mondo Chick") && window) {
            const auto id="mondo/"+std::to_string(cycle.count());
            if(scheduled_finished_!=cycle || (job_.id==id && launched_ && !visit_->finished))
                requested={id,"Mondo Chick",cycle,cycle+std::chrono::minutes(15),true};
        }
        if(requested.ends<=wall) requested={};
        select_job(std::move(requested));
        std::vector<coordination::RotationMember> members;unsigned unidentified=0;bool has_attack=false;
        attack_complete_|=bool(visit_->finished);
        for(const auto& peer:group.peers) if(!peer.host) {
            const auto& alt=topic(peer,"alt");const auto id=value<std::string>(alt,"id");
            if(id.empty()) { ++unidentified;continue; }
            const auto kind=value<std::string>(alt,"type");
            const bool attacker=kind=="Attack";
            const bool done=attacker && value<std::string>(alt,"attack-job")==job_.id && value<bool>(alt,"attack-done");
            const auto targets=value<SettingSelection>(alt,"attack-targets");
            const bool ready=value<bool>(alt,"running") && (!attacker || std::find(targets.begin(),targets.end(),job_.enemy)!=targets.end());
            has_attack|=attacker && ready;attack_complete_|=done;
            members.push_back({id,kind=="Guiding" || attacker,value<bool>(alt,"running"),
                value<bool>(alt,"active"),attacker?done:value<bool>(alt,"complete"),
                value<std::string>(alt,"epoch")==connection_?value<std::string>(alt,"ack"):std::string{},attacker,value<bool>(alt,"swappable"),ready});
        }
        const auto assignments=rotation_.update(members,slot_limit,unidentified,!job_.id.empty() && !attack_complete_ && bool(assistance_),job_.id);
        SettingSelection commands;unsigned admitted=0;
        for(const auto& item:assignments) { commands.push_back(item.id+"/"+item.command);admitted+=item.main; }
        if(job_.scheduled && attack_complete_) scheduled_finished_=job_.starts;
        attack_pending_=has_attack && !job_.id.empty() && job_.scheduled && !visit_->finished && (!attack_complete_ || launched_)
            && bool(assistance_) && !view.reconnecting && wall>=job_.starts-std::chrono::minutes(1);
        visit_->allowed=attack_pending_;
        state->publish("guiding.rotation",{{"epoch",connection_},{"commands",commands},{"attack-job",job_.id},{"attack-enemy",job_.enemy},
            {"attack-start",std::to_string(job_.starts.count())},{"attack-end",std::to_string(job_.ends.count())},{"attack-scheduled",job_.scheduled},
            {"attack-active",!job_.id.empty() && !attack_complete_ && !view.reconnecting},
            {"attack-ready",!view.reconnecting && (job_.scheduled?attack_pending_ && bool(visit_->arrived):!job_.id.empty())}});
        status_="Alt rotation · "+std::to_string(admitted)+" with main · "+std::to_string(assignments.size()-admitted)+" on public servers";
        return;
    }
    state->withdraw("guiding.rotation");
    if(!controller.joined() || controller.role()!=coordination::Role::guest) {
        visit_->allowed=false;visit_->main_ready=false;state->withdraw("alt");return;
    }
    const auto account=controller.account_id().toStdString();
    const coordination::Peer* main=nullptr;
    for(const auto& peer:group.peers) if(peer.host) { main=&peer;break; }
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
    if(main && (guiding || attack || swappable)) {
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
    if(!guiding && !attack && !swappable) { managed_=away_=false; }
    if(attack && main) {
        const auto& rotation=topic(*main,"guiding.rotation");
        const auto id=value<std::string>(rotation,"attack-job"),enemy=value<std::string>(rotation,"attack-enemy");
        const auto starts=timestamp(rotation,"attack-start"),ends=timestamp(rotation,"attack-end");
        const bool valid=game::request_identifier(id) && assistance_.destinations.contains(enemy)
            && starts>=Time{} && starts<=wall+std::chrono::minutes(3) && ends>wall && ends>starts && ends-starts<=std::chrono::hours(8);
        if(valid) select_job({id,enemy,starts,ends,value<bool>(rotation,"attack-scheduled")});
        const bool active=valid && value<bool>(rotation,"attack-active");
        visit_->main_ready=active && value<bool>(rotation,"attack-ready") && !view.reconnecting;
        const bool assigned=valid_command && desired_main_ && !acknowledged_.empty() && location_=="main";
        attack_pending_=running && active && assigned && !visit_->finished && bool(assistance_);
        visit_->allowed=attack_pending_ && !view.reconnecting;
    } else { visit_->allowed=false;visit_->main_ready=false; }
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
        status_=desired_main_?(location_=="main" && !acknowledged_.empty()?
            attack?"With main · "+job_.enemy:guiding?"With main · waiting for own Guiding Star":"With main":
            attack?"Joining main for "+job_.enemy:"Joining main"):
            (!acknowledged_.empty()?"Public server · running normal priorities":"Moving to a public server");
        if(desired_main_ && active_until_>view.now) status_="Holding main's slot · Guiding Star active";
    }
    if(attack && !assistance_) status_="Attack unavailable · Check movement speed and access to combat areas";
    SettingSelection targets;for(const auto& [enemy,_]:assistance_.destinations) targets.push_back(enemy);
    state->publish("alt",{{"id",account},{"type",std::string(attack?"Attack":guiding?"Guiding":"Support")},{"running",running && (!attack || bool(assistance_))},
        {"active",location_=="main" && (active_until_>view.now || (attack && launched_ && visit_->allowed && !visit_->finished))},{"complete",completed_},
        {"ack",acknowledged_},{"epoch",command_.substr(0,command_.find('/'))},{"where",location_},{"status",status_},
        {"swappable",swappable},{"attack-targets",targets},{"attack-job",job_.id},{"attack-done",bool(visit_->finished)}});
}
std::unique_ptr<automation::Program> RotationSync::program() const {
    if(!pending() || !assistance_) return {};
    auto program=assistance_.create(job_,!main_,saw_slot,visit_);
    if(program) launched_=true;
    return program;
}
}
