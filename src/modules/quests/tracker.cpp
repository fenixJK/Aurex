#include "hive/modules/quests/tracker.hpp"
#include <algorithm>

namespace hive::features::quests {
namespace {
bool valid_id(const std::string& id) {
    return !id.empty() && id.size()<=80 && std::none_of(id.begin(),id.end(),[](unsigned char c) { return c<32 || c==127; });
}
}
Tracker::Tracker(std::vector<Field> fields,Time freshness,int priority,Time retention)
    :fields_(std::move(fields)),freshness_(freshness),retention_(retention),priority_(priority) {
    if (fields_.empty() || fields_.size()>64 || freshness_<Time(1) || freshness_>Time(60000) || priority_<0 || priority_>100)
        throw std::invalid_argument("Invalid quest policy settings");
    if (retention_<Time::zero() || retention_>std::chrono::hours(1) || (retention_!=Time::zero() && retention_<freshness_))
        throw std::invalid_argument("Invalid quest knowledge lifetime");
    std::set<std::string> names;
    for (const auto& field : fields_) if (!valid_id(field.name) || !names.insert(field.name).second)
        throw std::invalid_argument("Invalid quest field catalog");
}
void Tracker::reset() { cards_.clear(); retained_.clear(); generation_=revision_=0; last_={}; window_={}; }
void Tracker::context(const ObservationContext& c) {
    if (!c.generation || !c.window.id || !c.window.bounds.valid() || c.now<Time::zero() || c.now>Time::max()-std::max(freshness_,retention_))
        throw std::invalid_argument("Invalid quest context");
    if (c.generation!=generation_ || c.window.id!=window_.id || c.window.process!=window_.process || c.window.bounds!=window_.bounds) {
        reset(); generation_=c.generation; window_=c.window;
    }
    if (c.now<last_) throw std::invalid_argument("Quest clock moved backwards");
    last_=c.now;
}
bool Tracker::observe(const ObservationContext& c,const Observation<Card>& observation) {
    context(c);
    if (!observation.has_value(c,freshness_)) return false;
    const auto& card=observation.value;
    if (!valid_id(card.giver) || !valid_id(card.quest) || card.objectives.empty() || card.objectives.size()>16)
        throw std::invalid_argument("Invalid quest card");
    std::set<std::string> ids;
    for (const auto& objective : card.objectives) {
        if (!valid_id(objective.id) || !ids.insert(objective.id).second || objective.text.size()>512
            || objective.kind<Kind::field_pollen || objective.kind>Kind::unsupported
            || (objective.known && (!objective.required || objective.collected>objective.required
                || (objective.kind==Kind::enemy && !std::set<std::string>{"Ladybug","Rhino Beetle","Spider","Mantis","Scorpion","Werewolf"}.contains(objective.enemy)))))
            throw std::invalid_argument("Invalid quest progress");
    }
    const auto previous=cards_.find(card.giver);
    if (previous!=cards_.end() && previous->second.captured>=observation.captured) return false;
    if (previous==cards_.end() && cards_.size()>=16) throw std::invalid_argument("Too many quest givers");
    const bool same_card=previous!=cards_.end() && previous->second.value.quest==card.quest;
    const bool changed_list=same_card && card.complete_view && previous->second.value.complete_view
        && (card.objectives.size()!=previous->second.value.objectives.size()
            || !std::equal(card.objectives.begin(),card.objectives.end(),previous->second.value.objectives.begin(),
                [](const auto& a,const auto& b) { return a.id==b.id; }));
    if (!same_card || changed_list) retained_.erase(card.giver);
    if (retention_>Time::zero()) {
        auto& memory=retained_[card.giver]; memory.quest=card.quest;
        for (const auto& objective : card.objectives) {
            if (objective.known && objective.kind!=Kind::unsupported)
                memory.rows[objective.id]={Knowledge::present,objective,observation.captured,observation.window,observation.generation};
            else memory.rows.erase(objective.id);
        }
        if (card.complete_view) std::erase_if(memory.rows,[&](const auto& row) { return !ids.contains(row.first); });
        std::erase_if(memory.rows,[&](const auto& row) { return !row.second.has_value(c,retention_); });
        if (memory.rows.empty()) retained_.erase(card.giver);
    }
    cards_[card.giver]=observation;
    return true;
}
Report Tracker::report(const ObservationContext& c,bool retained_only) {
    context(c); Report result; result.requests={generation_,++revision_,{}};
    for (const auto& [giver,observation] : cards_) {
        const auto memory=retained_.find(giver);
        const bool live=!retained_only && observation.has_value(c,freshness_);
        std::map<std::string,std::pair<Objective,Time>> rows;
        if (memory!=retained_.end() && memory->second.quest==observation.value.quest)
            for (const auto& [id,row] : memory->second.rows) if (row.has_value(c,retention_))
                rows[id]={row.value,std::min(c.now+freshness_,row.captured+retention_)};
        if (live) for (const auto& row : observation.value.objectives)
            rows[row.id]={row,observation.captured+freshness_};
        if (rows.empty()) { result.blocked.push_back(giver+": quest reading expired"); continue; }
        const auto& card=observation.value;
        bool complete=live && card.complete_view;
        if (!card.complete_view) result.blocked.push_back(giver+": quest view is incomplete");
        if (!live && std::any_of(card.objectives.begin(),card.objectives.end(),[](const auto& o) { return !o.known || o.kind==Kind::unsupported; }))
            result.blocked.push_back(giver+": unreadable or unsupported objective");
        for (const auto& [row_id,entry] : rows) {
            const auto& [objective,expires]=entry;
            if (!objective.known || objective.kind==Kind::unsupported) {
                complete=false; result.blocked.push_back(giver+": unreadable or unsupported objective"); continue;
            }
            const bool finished=objective.collected==objective.required;
            const bool field_objective=objective.kind==Kind::field_pollen || objective.kind==Kind::field_activity
                || objective.kind==Kind::any_pollen || objective.kind==Kind::gather_activity
                || objective.kind==Kind::red_pollen || objective.kind==Kind::blue_pollen || objective.kind==Kind::white_pollen;
            if (finished && !field_objective) continue;
            if (!finished) complete=false;
            if (objective.kind==Kind::feeding || objective.kind==Kind::challenge || objective.kind==Kind::petals) {
                result.actions.emplace_back(giver,objective); continue;
            }
            if (objective.kind==Kind::station) {
                static const std::map<std::string,std::string> stations{{"RedBoost","Red Field Booster"},{"BlueBoost","Blue Field Booster"}};
                const auto station=stations.find(objective.target);
                if (station==stations.end()) { result.blocked.push_back(giver+": "+objective.text+" needs manual completion"); continue; }
                const auto id=giver+"/"+objective.id;
                auto& requests=result.requests.channels["stations"];
                if (id.size()>80 || requests.size()>=16) result.blocked.push_back(giver+": request capacity reached");
                else requests.push_back({id,priority_,expires,{{"station",station->second}}});
                continue;
            }
            if (objective.kind==Kind::tracked) { result.blocked.push_back(giver+": "+objective.text+" needs manual completion"); continue; }
            if (objective.kind==Kind::any_enemy) {
                for (const auto* enemy:{"Ladybug","Rhino Beetle","Spider","Mantis","Scorpion","Werewolf"}) {
                    const auto id=giver+"/"+objective.id+"/"+enemy;
                    if (result.requests.enemies.size()<16 && id.size()<=80) result.requests.enemies.push_back({id,enemy,priority_,expires});
                }
                continue;
            }
            if (objective.kind==Kind::enemy) {
                const auto id=giver+"/"+objective.id;
                if (id.size()>80 || result.requests.enemies.size()>=16) result.blocked.push_back(giver+": request capacity reached");
                else result.requests.enemies.push_back({id,objective.enemy,priority_,expires});
                continue;
            }
            std::vector<std::string> targets;
            for (const auto& field : fields_) if ((objective.kind==Kind::field_pollen || objective.kind==Kind::field_activity)?field.name==objective.field:
                objective.kind==Kind::any_pollen || objective.kind==Kind::gather_activity || field.pollen.contains(objective.kind)) targets.push_back(field.name);
            if (targets.empty()) { result.blocked.push_back(giver+": no enabled field for objective"); continue; }
            const auto id=giver+"/"+objective.id;
            if (id.size()>80 || result.requests.fields.size()+result.completed_fields.size()>=16) { result.blocked.push_back(giver+": request capacity reached"); continue; }
            game::FieldRequest request{id,targets.front(),priority_,expires};
            request.alternatives.assign(targets.begin()+1,targets.end()); request.complete=finished;
            (finished?result.completed_fields:result.requests.fields).push_back(std::move(request));
        }
        if (complete) result.ready.push_back(giver);
    }
    return result;
}
}
