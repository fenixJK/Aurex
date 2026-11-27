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
std::vector<SlotAssignment> SlotRotation::update(const std::vector<RotationMember>& members,unsigned slots,unsigned reserved,bool attack_due,const std::string& attack_job) {
    unsigned residents=1+std::min(reserved,5u); // Main plus slots reserved for accounts outside this group.
    std::map<std::string,const RotationMember*> online;
    for(const auto& member:members) {
        if(member.id.empty() || online.contains(member.id)) continue;
        online[member.id]=&member;
        if(!member.guiding && !member.attack && !member.swappable) { residents_.insert(member.id);entries_.erase(member.id);continue; }
        residents_.erase(member.id);
        auto [it,inserted]=entries_.try_emplace(member.id);
        auto& entry=it->second;
        entry.guiding=member.guiding;
        if(inserted) {
            entry.assignment.id=member.id;assign(entry,!member.guiding && !member.attack);
            // New attack alts first confirm a public-server location before admission.
            if(member.attack) entry.occupies=true;
        }
        // A missing peer or a requested departure is not proof that a Roblox slot is free.
        if(!entry.assignment.main && member.acknowledged==entry.assignment.command) entry.occupies=false;
        if(entry.assignment.main && member.acknowledged==entry.assignment.command)
            entry.completed=member.turn_complete && !member.star_active;
    }
    residents+=unsigned(residents_.size());
    const unsigned capacity=residents<6?6-residents:0u;
    const auto guiding_limit=std::min(std::clamp(slots,1u,5u),capacity);
    unsigned occupied=0,guiding_occupied=0;
    for(const auto& [id,entry]:entries_) { occupied+=entry.occupies;guiding_occupied+=entry.occupies && entry.guiding; }
    const auto waiting=[&](const auto& pair) {
        const auto member=online.find(pair.first);
        return member!=online.end() && member->second->guiding && member->second->running
            && !pair.second.assignment.main && !pair.second.occupies;
    };
    unsigned waiting_count=unsigned(std::count_if(entries_.begin(),entries_.end(),waiting));
    unsigned retained=unsigned(std::count_if(entries_.begin(),entries_.end(),[](const auto& pair) {
        return pair.second.assignment.main && pair.second.guiding;
    }));
    for(auto& [id,entry]:entries_) {
        if(!entry.assignment.main) continue;
        const auto member=online.find(id);
        if(member==online.end() || !member->second->guiding || member->second->star_active) continue;
        if((entry.completed && waiting_count) || retained>guiding_limit) {
            assign(entry,false);--retained;
            if(waiting_count) --waiting_count;
        }
    }
    if(!attack_due || attack_job_!=attack_job) attacker_.clear();
    attack_job_=attack_job;
    auto attacker=entries_.find(attacker_);
    if(attack_due && attacker_.empty()) for(auto it=entries_.begin();it!=entries_.end();++it) {
        const auto member=online.find(it->first);
        if(member==online.end() || !member->second->attack || !member->second->running || member->second->turn_complete) continue;
        if(attacker==entries_.end() || (it->second.assignment.main && !attacker->second.assignment.main)
            || (it->second.assignment.main==attacker->second.assignment.main && it->second.order<attacker->second.order)) attacker=it;
    }
    if(attacker!=entries_.end()) attacker_=attacker->first;
    for(auto it=entries_.begin();it!=entries_.end();++it) {
        const auto member=online.find(it->first);
        if(member!=online.end() && member->second->attack && it!=attacker && it->second.assignment.main) assign(it->second,false);
    }
    const auto priority=[&](const auto& pair) {
        const auto member=online.find(pair.first);
        if(member==online.end() || !member->second->running) return 0;
        if(member->second->attack) return attacker!=entries_.end() && attacker->first==pair.first?3:0;
        return member->second->guiding?1:2;
    };
    // A waiting attack alt may borrow one ordinary support slot. Departures must
    // finish before another seat is lent; an active Guiding Star is never evicted.
    const bool departing=std::any_of(entries_.begin(),entries_.end(),[](const auto& pair) { return pair.second.occupies && !pair.second.assignment.main; });
    int wanted=0;
    for(const auto& pair:entries_) if(!pair.second.assignment.main) wanted=std::max(wanted,priority(pair));
    if(!departing && occupied>=capacity && wanted>1) {
        auto lend=entries_.end();
        for(auto it=entries_.begin();it!=entries_.end();++it) {
            const auto member=online.find(it->first);
            if(!it->second.assignment.main || member==online.end() || !member->second->running || member->second->star_active
                || priority(*it)>=wanted) continue;
            if(lend==entries_.end() || priority(*it)<priority(*lend)) lend=it;
        }
        if(lend!=entries_.end()) assign(lend->second,false);
    }
    // Release acknowledgements come on a later update; never grant two accounts the same seat.
    while(occupied<capacity) {
        auto best=entries_.end();
        for(auto it=entries_.begin();it!=entries_.end();++it) {
            const auto rank=priority(*it);
            if(!rank || it->second.assignment.main || it->second.occupies || (rank==1 && guiding_occupied>=guiding_limit)) continue;
            if(best==entries_.end() || rank>priority(*best) || (rank==priority(*best) && it->second.order<best->second.order)) best=it;
        }
        if(best==entries_.end()) break;
        if(priority(*best)==1) ++guiding_occupied;
        assign(best->second,true);++occupied;
    }
    std::vector<SlotAssignment> result;
    for(const auto& [id,entry]:entries_) if(online.contains(id)) result.push_back(entry.assignment);
    // Retain disconnected occupants, but discard departed accounts that do not hold a seat.
    std::erase_if(entries_,[&](const auto& pair) { return !online.contains(pair.first) && !pair.second.occupies; });
    return result;
}
}
