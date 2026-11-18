#include "defeats.hpp"
#include "game/data/fields.hpp"
#include <algorithm>

namespace hive::features::combat {
DefeatCooldowns::DefeatCooldowns(const Plan& plan,std::shared_ptr<game::Cooldowns> saved):saved_(std::move(saved)) {
    for (const auto& enemy:plan.enemies) {
        // Shared boss notices can describe another player's kill. Those retain
        // their encounter-specific completion and scheduling rules.
        if (enemy.boss || !enemy.observations || enemy.cooldown<=Time{}) continue;
        auto source=std::find_if(sources_.begin(),sources_.end(),[&](const auto& s) { return s.observations==enemy.observations; });
        if (source==sources_.end()) { sources_.push_back({enemy.observations}); source=sources_.end()-1; }
        const auto field=game::field_at(enemy.destination);
        const bool unique=std::count_if(plan.enemies.begin(),plan.enemies.end(),[&](const auto& e) { return e.species==enemy.species; })==1;
        source->targets.push_back({enemy.id,enemy.species,field?field->id:std::string{},enemy.count,enemy.cooldown,enemy.respawn_reduction,unique});
    }
}
void DefeatCooldowns::configure(int reduction) {
    reduction_=reduction;
    generation_=session_=0; field_.clear();
    for (auto& source:sources_) { source.last_event=0; for (auto& target:source.targets) target.kills.clear(); }
}
void DefeatCooldowns::update(const extensions::SessionView& view) {
    if (!saved_ || view.status.state!=RunState::running || !view.generation || view.reconnecting) return;
    if (generation_!=view.generation || session_!=view.session_id) {
        configure(reduction_); generation_=view.generation; session_=view.session_id;
    }
    std::string field;
    if (view.feature) for (const auto& candidate:game::fields())
        if (view.feature->field==candidate.id || view.feature->field==candidate.name) { field=candidate.id; break; }
    if (field!=field_) { field_=std::move(field); field_since_=view.now; }
    for (auto& source:sources_) {
        const auto result=source.observations->peek();
        if (!result || result->context.generation!=view.generation) continue;
        const ObservationContext context{result->context.window,view.generation,view.now};
        const auto reading=game::read_enemies(*source.observations,context);
        if (!reading) continue;
        for (const auto& event:reading->recent_defeats) {
            if (event.id<=source.last_event || event.seen>view.now) continue;
            source.last_event=event.id;
            if (view.now-event.seen>Time(2000)) continue;
            for (auto& target:source.targets) {
                if (target.species!=event.name) continue;
                // A unique mob has one spawn. Repeated species need a known
                // current field; never apply a Rhino/Ladybug kill to every field.
                if (!target.unique && (target.field.empty() || target.field!=field_ || event.seen<field_since_)) continue;
                const auto cooldown=target.reduced?Time((target.cooldown.count()*(100-reduction_)+99)/100):target.cooldown;
                std::erase_if(target.kills,[&](Time seen) { return event.seen-seen>=std::min(cooldown,Time(180000)); });
                target.kills.push_back(event.seen);
                if (target.kills.size()<target.count) continue;
                saved_->load();
                saved_->remember(target.id,event.seen,event.seen+cooldown,true,context);
                target.kills.clear();
            }
        }
    }
}
}
