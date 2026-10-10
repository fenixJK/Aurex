#include "routine.hpp"
#include <algorithm>

namespace hive::extensions {
Routine::Routine(std::vector<RoutineEntry> entries,std::unique_ptr<automation::ActivityPolicy> policy)
    :entries_(std::move(entries)),due_(entries_.size()),deferred_until_(entries_.size()),after_work_(entries_.size()),pending_at_dispatch_(entries_.size()),policy_(std::move(policy)) {
    if (!policy_ || entries_.empty() || entries_.size()>32) throw std::invalid_argument("Enable at least one routine feature");
    std::set<std::string> ids;
    unsigned fallbacks=0;
    for (const auto& e : entries_) {
        const auto& p=e.participation;
        validate(p);
        if (e.id.empty() || !e.revision || !ids.insert(e.id).second || (p.fallback && ++fallbacks>1))
            throw std::invalid_argument("Invalid routine participation");
    }
}
Routine::Readiness Routine::readiness(Host& host,const SessionView& view,std::size_t index) const {
    const auto& e=entries_[index];
    const auto& p=e.participation;
    if (deferred_until_[index]>view.now || after_work_[index]>completed_work_) return {};
    if (!p.fallback && !p.demand_only && !p.reactive && due_[index]>view.now) return {};
    const auto found=host.entries().find(e.id);
    if (found==host.entries().end() || !found->second.executable()) return {};
    const bool pending=!p.fallback && host.pending_work(e.id,view);
    if (!found->second.executable()) return {};
    return {pending,p.fallback || (p.demand_only?pending:(due_[index]<=view.now || (p.reactive && pending)))};
}
bool Routine::should_yield(Host& host,const SessionView& view) const {
    if (!active_ || !view.feature || view.feature->state!=automation::State::running) return false;
    const bool gathering=entries_[*active_].participation.fallback;
    const auto priority=entries_[*active_].participation.priority;
    for (std::size_t i=0;i<entries_.size();++i) if (i!=*active_ && !entries_[i].participation.fallback
        && (gathering?entries_[i].participation.interrupt_gather:entries_[i].participation.priority>priority)) {
        const auto work=readiness(host,view,i);
        if (work.eligible && work.pending && (gathering || !entries_[i].participation.demand_only || !pending_at_dispatch_[i])) return true;
    }
    return false;
}
void Routine::check(Time now) {
    if (now<last_ || now>Time::max()-std::chrono::hours(24)) throw std::runtime_error("Invalid routine clock");
    last_=now;
}
std::optional<automation::Launch> Routine::next(Host& host,const SessionView& view,bool allow_launch) {
    check(view.now);
    if (view.status.state!=RunState::running || !view.generation) throw std::runtime_error("Routine session stopped");
    if (!generation_) { generation_=view.generation; session_id_=view.session_id; }
    if (session_id_!=view.session_id || (generation_!=view.generation && (!session_id_ || !active_ || view.generation<generation_)))
        throw std::runtime_error("Routine session changed");
    if (generation_!=view.generation) std::fill(after_work_.begin(),after_work_.end(),0);
    generation_=view.generation;
    for (const auto& e : entries_) {
        const auto found=host.entries().find(e.id);
        if (found==host.entries().end() || (found->second.active() && found->second.revision()!=e.revision))
            throw std::runtime_error("Routine module changed: "+e.id);
    }
    if (active_) {
        if (!view.feature || view.feature->module_id!=entries_[*active_].id) throw std::runtime_error("Routine lost its active feature");
        if (view.feature->state==automation::State::running) return {};
        if (!automation::can_handoff(*view.feature)) throw std::runtime_error(view.feature->message.empty()?"Routine feature did not complete":view.feature->message);
        const auto& e=entries_[*active_];
        if (view.feature->state==automation::State::completed) ++completed_work_;
        const bool failed=view.feature->state==automation::State::failed;
        after_work_[*active_]=e.participation.demand_only && view.feature->state==automation::State::deferred?completed_work_+1:0;
        due_[*active_]=view.now+(failed?std::max(e.participation.recheck,Time(30000)):e.participation.recheck);
        deferred_until_[*active_]=failed || view.feature->state==automation::State::deferred?due_[*active_]:Time{};
        policy_->completed(e.participation.fallback && !e.participation.priority?std::nullopt:std::optional(e.id));
        active_.reset();
    } else if (view.feature && view.feature->state==automation::State::running) throw std::runtime_error("Another feature owns the session");
    if (!allow_launch) return {};
    std::vector<automation::ActivityOption> options;
    std::fill(pending_at_dispatch_.begin(),pending_at_dispatch_.end(),false);
    std::optional<std::size_t> fallback;
    bool interrupt_gather=false;
    for (std::size_t i=0;i<entries_.size();++i) {
        const auto& e=entries_[i];
        const auto work=readiness(host,view,i);
        pending_at_dispatch_[i]=work.pending;
        if (!work.eligible) continue;
        if (e.participation.fallback) {
            fallback=i;
            if (e.participation.priority>0 && due_[i]<=view.now)
                options.push_back({e.id,e.participation.priority,due_[i]});
        }
        else {
            options.push_back({e.id,e.participation.priority,std::min(due_[i],view.now)});
            interrupt_gather=interrupt_gather || (e.participation.interrupt_gather && work.pending);
        }
    }
    if (interrupt_gather && fallback) {
        std::erase_if(options,[&](const auto& option) { return option.id==entries_[*fallback].id; });
        fallback.reset();
    }
    auto choice=policy_->choose(options);
    if (!choice && (!fallback || entries_[*fallback].participation.priority>0)) {
        policy_->completed({}); choice=policy_->choose(options);
    }
    std::optional<std::size_t> selected=fallback;
    if (choice) {
        if (std::none_of(options.begin(),options.end(),[&](const auto& o) { return o.id==*choice; }))
            throw std::runtime_error("Routine policy chose unavailable work");
        selected=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e) { return e.id==*choice; })-entries_.begin();
    }
    if (!selected) return {};
    if (!host.entries().at(entries_[*selected].id).executable()) return {};
    try {
        auto launch=host.prepare_routine(entries_[*selected]);
        active_=selected;
        return launch;
    } catch (const std::exception& e) { host.fail_module(entries_[*selected].id,e.what()); }
    catch (...) { host.fail_module(entries_[*selected].id,"Module could not start"); }
    return {};
}
}
