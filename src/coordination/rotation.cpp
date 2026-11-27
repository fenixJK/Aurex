#include "rotation.hpp"
#include <algorithm>

namespace hive::coordination {
void SlotRotation::assign(Entry& entry,bool main) {
    entry.assignment.main=main;
    entry.assignment.command=std::to_string(++serial_)+(main?"m":"p");
    entry.completed=false;
    if(main) entry.occupies=true;
    else entry.order=++order_;
}
std::vector<SlotAssignment> SlotRotation::update(const std::vector<RotationMember>& members,unsigned slots,unsigned unidentified,bool request_due,const std::string& request) {
    unsigned residents=1+std::min(unidentified,5u); // Count connected accounts before their role is known.
    std::map<std::string,const RotationMember*> online;
    for(const auto& member:members) {
        if(member.id.empty() || online.contains(member.id)) continue;
        online[member.id]=&member;
        if(!member.rotational && !member.swappable) { residents_.insert(member.id);entries_.erase(member.id);continue; }
        const bool was_resident=residents_.erase(member.id)>0;
        auto [it,inserted]=entries_.try_emplace(member.id);
        auto& entry=it->second;entry.rotational=member.rotational;
        if(inserted) {
            entry.assignment.id=member.id;assign(entry,!member.rotational);
            // An on-request visitor confirms its public-server location before admission.
            if(member.on_request || was_resident) entry.occupies=true;
        }
        if(!entry.assignment.main && member.acknowledged==entry.assignment.command) entry.occupies=false;
        if(entry.assignment.main && member.acknowledged==entry.assignment.command)
            entry.completed=member.turn_complete && !member.holding;
    }
    residents+=unsigned(residents_.size());
    const unsigned capacity=residents<6?6-residents:0u;
    const auto rotation_limit=std::min(std::clamp(slots,1u,5u),capacity);
    if(!request_due || request_!=request) visitor_.clear();
    request_=request;
    auto visitor=entries_.find(visitor_);
    if(request_due && visitor_.empty()) for(auto it=entries_.begin();it!=entries_.end();++it) {
        const auto member=online.find(it->first);
        if(member==online.end() || !member->second->on_request || !member->second->running || !member->second->available || member->second->turn_complete) continue;
        if(visitor==entries_.end() || (it->second.assignment.main && !visitor->second.assignment.main)
            || (it->second.assignment.main==visitor->second.assignment.main && it->second.order<visitor->second.order)) visitor=it;
    }
    if(visitor!=entries_.end()) visitor_=visitor->first;
    const auto priority=[&](const auto& pair) {
        const auto member=online.find(pair.first);
        if(member==online.end() || !member->second->running || !member->second->available) return 0;
        if(member->second->on_request) return visitor!=entries_.end() && visitor->first==pair.first?3:0;
        return member->second->rotational?1:2;
    };
    const auto releasable=[&](const auto& pair) {
        const auto member=online.find(pair.first);
        return pair.second.assignment.main && member!=online.end() && member->second->running && !member->second->holding;
    };
    for(auto it=entries_.begin();it!=entries_.end();++it) {
        const auto member=online.find(it->first);
        if(member!=online.end() && member->second->on_request && it!=visitor && releasable(*it)) assign(it->second,false);
    }
    unsigned retained=0,rotating=0;
    for(const auto& [id,entry]:entries_) { retained+=entry.assignment.main;rotating+=entry.assignment.main && entry.rotational; }
    // Reducing either limit uses the same release rules as an ordinary handoff.
    while(retained>capacity || rotating>rotation_limit) {
        auto release=entries_.end();
        for(auto it=entries_.begin();it!=entries_.end();++it) {
            if(!releasable(*it) || (rotating>rotation_limit && !it->second.rotational)) continue;
            if(release==entries_.end() || priority(*it)<priority(*release)) release=it;
        }
        if(release==entries_.end()) break;
        --retained;rotating-=release->second.rotational;assign(release->second,false);
    }
    unsigned occupied=0,rotation_occupied=0;
    for(const auto& [id,entry]:entries_) { occupied+=entry.occupies;rotation_occupied+=entry.occupies && entry.rotational; }
    const auto waiting=[&](const auto& pair) { return priority(pair)>0 && !pair.second.assignment.main && !pair.second.occupies; };
    // All rotating purposes share this budget, including departures and missing peers.
    while(occupied<capacity) {
        auto best=entries_.end();
        for(auto it=entries_.begin();it!=entries_.end();++it) {
            if(!waiting(*it) || (it->second.rotational && rotation_occupied>=rotation_limit)) continue;
            if(best==entries_.end() || priority(*it)>priority(*best)
                || (priority(*it)==priority(*best) && it->second.order<best->second.order)) best=it;
        }
        if(best==entries_.end()) break;
        rotation_occupied+=best->second.rotational;assign(best->second,true);++occupied;
    }
    const bool departing=std::any_of(entries_.begin(),entries_.end(),[](const auto& pair) { return pair.second.occupies && !pair.second.assignment.main; });
    if(!departing) {
        auto next=entries_.end();
        for(auto it=entries_.begin();it!=entries_.end();++it) if(waiting(*it))
            if(next==entries_.end() || priority(*it)>priority(*next)
                || (priority(*it)==priority(*next) && it->second.order<next->second.order)) next=it;
        if(next!=entries_.end()) {
            const bool rotation_full=next->second.rotational && rotation_occupied>=rotation_limit;
            auto release=entries_.end();
            for(auto it=entries_.begin();it!=entries_.end();++it) {
                if(!releasable(*it) || (rotation_full && !it->second.rotational)) continue;
                if(priority(*it)>priority(*next) || (priority(*it)==priority(*next) && !it->second.completed)) continue;
                if(release==entries_.end() || priority(*it)<priority(*release)) release=it;
            }
            if(release!=entries_.end()) assign(release->second,false);
        }
    }
    std::vector<SlotAssignment> result;
    for(const auto& [id,entry]:entries_) if(online.contains(id)) result.push_back(entry.assignment);
    std::erase_if(entries_,[&](const auto& pair) { return !online.contains(pair.first) && !pair.second.occupies; });
    return result;
}
}
