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
std::vector<SlotAssignment> SlotRotation::update(const std::vector<RotationMember>& members,unsigned slots,unsigned reserved) {
    unsigned residents=1+std::min(reserved,5u); // Main plus slots reserved for accounts outside this group.
    std::map<std::string,const RotationMember*> online;
    for(const auto& member:members) {
        if(member.id.empty() || online.contains(member.id)) continue;
        online[member.id]=&member;
        if(!member.guiding) { residents_.insert(member.id);entries_.erase(member.id);continue; }
        residents_.erase(member.id);
        auto [it,inserted]=entries_.try_emplace(member.id);
        auto& entry=it->second;
        if(inserted) { entry.assignment.id=member.id;assign(entry,false); }
        // A missing peer or a requested departure is not proof that a Roblox slot is free.
        if(!entry.assignment.main && member.acknowledged==entry.assignment.command) entry.occupies=false;
        if(entry.assignment.main && member.acknowledged==entry.assignment.command)
            entry.completed=member.turn_complete && !member.star_active;
    }
    residents+=unsigned(residents_.size());
    const unsigned capacity=std::min(std::clamp(slots,1u,5u),residents<6?6-residents:0u);
    unsigned occupied=0;
    for(const auto& [id,entry]:entries_) occupied+=entry.occupies;
    const auto waiting=[&](const auto& pair) {
        const auto member=online.find(pair.first);
        return member!=online.end() && member->second->guiding && member->second->running
            && !pair.second.assignment.main && !pair.second.occupies;
    };
    unsigned waiting_count=unsigned(std::count_if(entries_.begin(),entries_.end(),waiting));
    unsigned retained=unsigned(std::count_if(entries_.begin(),entries_.end(),[](const auto& pair) { return pair.second.assignment.main; }));
    for(auto& [id,entry]:entries_) {
        if(!entry.assignment.main) continue;
        const auto member=online.find(id);
        if(member==online.end() || member->second->star_active) continue;
        if((entry.completed && waiting_count) || retained>capacity) {
            assign(entry,false);--retained;
            if(waiting_count) --waiting_count;
        }
    }
    // Release acknowledgements come on a later update; never grant two accounts the same seat.
    while(occupied<capacity) {
        auto best=entries_.end();
        for(auto it=entries_.begin();it!=entries_.end();++it)
            if(waiting(*it) && (best==entries_.end() || it->second.order<best->second.order)) best=it;
        if(best==entries_.end()) break;
        assign(best->second,true);++occupied;
    }
    std::vector<SlotAssignment> result;
    for(const auto& [id,entry]:entries_) if(online.contains(id)) result.push_back(entry.assignment);
    // Retain disconnected occupants, but discard departed accounts that do not hold a seat.
    std::erase_if(entries_,[&](const auto& pair) { return !online.contains(pair.first) && !pair.second.occupies; });
    return result;
}
}
