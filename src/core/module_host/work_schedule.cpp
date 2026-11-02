#include "work_schedule.hpp"
#include <algorithm>

namespace hive::extensions {
void validate(const WorkEstimate& work) {
    if (work.label.empty() || work.label.size()>160 || work.destination.size()>80
        || work.duration<Time{} || work.duration>std::chrono::hours(24) || work.ready<Time{}
        || (work.finish_by && *work.finish_by<Time{}) || work.requested_by.size()>32
        || std::any_of(work.requested_by.begin(),work.requested_by.end(),[](const auto& owner) { return owner.empty() || owner.size()>80; }))
        throw std::invalid_argument("Invalid work estimate");
}
void validate(const WorkWindow& window) {
    if (window.label.empty() || window.label.size()>160 || window.until<Time{} || window.allowed_modules.size()>32
        || std::any_of(window.allowed_modules.begin(),window.allowed_modules.end(),[](const auto& id) { return id.empty() || id.size()>80; })) throw std::invalid_argument("Invalid work window");
}
const WorkEstimate* WorkSnapshot::find(const std::string& owner) const {
    const auto found=std::find_if(offers.begin(),offers.end(),[&](const auto& offer) { return offer.owner==owner; });
    return found==offers.end()?nullptr:&found->work;
}
Time WorkSnapshot::wait_until(const std::string& owner,Time now) const {
    const auto* work=find(owner);
    if (!work) return now;
    auto after=now;
    for (const auto& protected_work:windows)
        if (protected_work.owner!=owner && !protected_work.window.allowed_modules.contains(owner)
            && !(protected_work.window.allow_requested_work && work->requested_by.contains(protected_work.owner)))
        {
            auto until=protected_work.window.until;
            if (protected_work.window.allow_deadlines && work->finish_by) until=std::min(until,*work->finish_by-work->duration);
            after=std::max(after,until);
        }
    return std::max({now,work->ready,after});
}
bool WorkSnapshot::can_start_window(const std::string& owner,Time now,Time duration) const {
    return std::none_of(offers.begin(),offers.end(),[&](const auto& offer) {
        const auto& work=offer.work;
        return offer.owner!=owner && work.finish_by && work.ready<=now && *work.finish_by-work.duration<now+duration;
    });
}
WorkSnapshot WorkSchedule::read() const { std::lock_guard lock(mutex_); return snapshot_; }
void WorkSchedule::publish(WorkSnapshot snapshot) { std::lock_guard lock(mutex_); snapshot_=std::move(snapshot); }
}
