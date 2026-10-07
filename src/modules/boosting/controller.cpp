#include "hive/modules/boosting/module.hpp"
#include <algorithm>

namespace hive::features::boosting {
void validate(const Preferences& preferences) {
    for (const auto& rule:preferences.items)
        if (rule.when<UseWhen::off || rule.when>UseWhen::boosted_conversion || rule.interval<Time(1000)
            || rule.interval>std::chrono::hours(24) || rule.backpack<1 || rule.backpack>100)
            throw std::invalid_argument("Invalid consumable rule");
    const auto& boost=preferences.boost;
    if (boost.dice_slot>7 || boost.glitter_slot>7 || boost.dice_limit<1 || boost.dice_limit>10000
        || boost.glitter_limit<1 || boost.glitter_limit>1000 || boost.roll_interval<Time(2000) || boost.roll_interval>Time(10000)
        || boost.extend_after<std::chrono::minutes(10) || boost.extend_after>std::chrono::minutes(14))
        throw std::invalid_argument("Invalid boost item settings");
    if (boost.dice_slot && boost.dice_slot==boost.glitter_slot) throw std::invalid_argument("Dice and glitter need different hotbar slots");
    for (const auto slot:{boost.dice_slot,boost.glitter_slot})
        if (slot && preferences.items[slot-1].when!=UseWhen::off) throw std::invalid_argument("Turn off the item rule for a dice or glitter slot");
}
void Controller::configure(Preferences preferences) {
    validate(preferences);
    std::lock_guard lock(mutex_); preferences_=std::move(preferences); enabled_=true;
}
void Controller::stop() {
    std::lock_guard lock(mutex_); enabled_=false; running_=false;
}
void Controller::update(const extensions::SessionView& view,const std::optional<game::DetectionResult>& reading) {
    std::lock_guard lock(mutex_);
    if (view.session_id && view.session_id!=session_) {
        session_=view.session_id; used_at_={}; uses_={}; visits_={}; visit_=0; next_item_={}; attempted_.clear();
        dice_used_=glitter_used_=0; ages_.clear(); boosted_.clear(); active_.clear(); completed_.clear(); readable_=false;
        observed_={}; gathering_field_.clear(); last_field_.clear();
    }
    if (view.generation!=generation_) {
        generation_=view.generation; active_.clear(); completed_.clear(); boosted_.clear(); ages_.clear();
        readable_=false; observed_={}; gathering_field_.clear(); last_field_.clear();
    }
    running_=enabled_ && view.status.state==RunState::running; updated_=view.now;
    if (!running_) return;
    if (reading && reading->error.empty() && reading->context.generation==generation_ && reading->context.now>observed_
        && reading->context.now<=view.now && view.now-reading->context.now<=Time(3000)) {
        const auto data=std::dynamic_pointer_cast<const game::FieldBoostDetection>(reading->value);
        if (data && data->readable) {
            const bool continuous=readable_ && reading->context.now-observed_<=Time(3000);
            const auto previous=boosted_;
            for (const auto& boost:data->boosts) if (!previous.contains(boost.field)) {
                const bool rolled=last_dice_ && reading->context.now>=*last_dice_
                    && reading->context.now-*last_dice_<=Time(10000);
                ages_[boost.field]={continuous || rolled?std::optional<Time>(reading->context.now):std::nullopt,false};
            }
            observed_=reading->context.now; readable_=true;
            boosted_.clear(); for (const auto& boost:data->boosts) boosted_.insert(boost.field);
            std::erase_if(ages_,[&](const auto& entry) { return !boosted_.contains(entry.first); });
        }
    }
    if (view.now-observed_>Time(3000)) readable_=false;
    if (fresh(view.now)) {
        std::string selected;
        if (preferences_.boost.dice_slot && !preferences_.boost.field.empty()) {
            const bool awaiting_roll=last_dice_ && observed_<*last_dice_+preferences_.boost.roll_interval;
            if (can_roll() || awaiting_roll || boosted_.contains(preferences_.boost.field)) selected=preferences_.boost.field;
        }
        if (selected.empty() && preferences_.follow) {
            if (boosted_.contains(active_) && std::find(preferences_.fields.begin(),preferences_.fields.end(),active_)!=preferences_.fields.end()) selected=active_;
            else for (const auto& field:preferences_.fields) if (boosted_.contains(field)) { selected=field; break; }
        }
        if (selected!=active_) { if (!active_.empty()) completed_=active_; active_=std::move(selected); }
        if (completed_==active_) completed_.clear();
    }
}
bool Controller::fresh(Time now) const { return readable_ && now>=observed_ && now-observed_<=Time(3000); }
bool Controller::can_roll() const { return preferences_.boost.dice_slot && !preferences_.boost.field.empty() && dice_used_<preferences_.boost.dice_limit; }
unsigned Controller::take_boost(const automation::HotbarContext& context) {
    if (!fresh(context.observation.now) || context.activity!=WorkActivity::gathering || context.field.empty()) return 0;
    const auto now=context.observation.now;
    const auto& boost=preferences_.boost;
    if (can_roll() && context.field==boost.field && !boosted_.contains(context.field)
        && (!last_dice_ || observed_>=*last_dice_+boost.roll_interval)) {
        last_dice_=now; ++dice_used_; return boost.dice_slot;
    }
    const bool allowed=boost.dice_slot?context.field==boost.field:
        std::find(preferences_.fields.begin(),preferences_.fields.end(),context.field)!=preferences_.fields.end();
    if (boost.glitter_slot && glitter_used_<boost.glitter_limit && allowed && boosted_.contains(context.field)
        && (!last_glitter_ || now-*last_glitter_>=std::chrono::seconds(905))) {
        auto& age=ages_.at(context.field);
        if (age.started && !age.extended && now-*age.started>=boost.extend_after && now-*age.started<std::chrono::minutes(15)) {
            age.extended=true; last_glitter_=now; ++glitter_used_; return boost.glitter_slot;
        }
    }
    return 0;
}
unsigned Controller::take(const automation::HotbarContext& context) {
    std::lock_guard lock(mutex_);
    const auto now=context.observation.now;
    if (!enabled_ || !running_ || !generation_ || generation_!=context.observation.generation
        || now<updated_ || now-updated_>Time(2000) || now<next_item_) return 0;
    const bool gathering=context.activity==WorkActivity::gathering && !context.field.empty();
    if (gathering && context.field!=gathering_field_) { gathering_field_=context.field; last_field_=context.field; ++visit_; }
    if (context.field.empty() || context.activity==WorkActivity::converting) gathering_field_.clear();
    if (const auto slot=take_boost(context)) { next_item_=now+Time(500); return slot; }
    const bool boosted=gathering && fresh(now) && boosted_.contains(context.field);
    for (unsigned i=0;i<preferences_.items.size();++i) {
        const auto& rule=preferences_.items[i];
        if (rule.when==UseWhen::off || (rule.limit && uses_[i]>=rule.limit)
            || (uses_[i] && now-used_at_[i]<rule.interval)) continue;
        const bool allowed=rule.when==UseWhen::gathering?gathering:rule.when==UseWhen::boosted?boosted:
            rule.when==UseWhen::field_start?gathering && visits_[i]!=visit_:
            rule.when==UseWhen::converting?context.activity==WorkActivity::converting:
            rule.when==UseWhen::backpack?gathering && context.backpack && *context.backpack>=rule.backpack:
            rule.when==UseWhen::boosted_conversion?context.activity==WorkActivity::converting && fresh(now) && boosted_.contains(last_field_):false;
        if (!allowed) continue;
        ++uses_[i]; used_at_[i]=now; visits_[i]=visit_; next_item_=now+Time(500);
        return i+1;
    }
    return 0;
}
game::Requests Controller::requests(Time now,std::uint64_t generation) const {
    std::lock_guard lock(mutex_); game::Requests result; result.generation=generation; result.revision=std::uint64_t(now.count());
    if (!enabled_ || !running_ || generation!=generation_) return result;
    if (!active_.empty() && fresh(now) && (preferences_.follow || preferences_.boost.dice_slot))
        result.fields.push_back({"boost/"+active_,active_,80,now+Time(3000),{},false,0,preferences_.interrupt});
    if (!completed_.empty()) result.fields.push_back({"boost/"+completed_,completed_,80,now+Time(3000),{},true});
    return result;
}
std::optional<std::string> Controller::next_booster(Time now,const std::vector<Booster>& boosters,const std::set<std::string>& requested) const {
    std::lock_guard lock(mutex_);
    if (!enabled_ || !running_) return {};
    for (const auto& booster:boosters) if (requested.contains(booster.name) && booster.visit && booster.remaining && booster.remaining()==Time{}) {
        const auto attempted=attempted_.find(booster.name);
        if (attempted==attempted_.end() || now-attempted->second>=std::chrono::minutes(5)) return booster.name;
    }
    if (!fresh(now) || (!active_.empty() && boosted_.contains(active_))) return {};
    for (const auto& name:preferences_.boosters) {
        const auto found=std::find_if(boosters.begin(),boosters.end(),[&](const auto& b) { return b.name==name; });
        if (found==boosters.end() || !found->visit || !found->remaining || found->remaining()>Time{}) continue;
        if (!found->fields.empty() && std::none_of(found->fields.begin(),found->fields.end(),[&](const auto& field) {
            return preferences_.boost.dice_slot?field==preferences_.boost.field:
                std::find(preferences_.fields.begin(),preferences_.fields.end(),field)!=preferences_.fields.end();
        })) continue;
        const auto attempted=attempted_.find(name);
        if (attempted==attempted_.end() || now-attempted->second>=std::chrono::minutes(5)) return name;
    }
    return {};
}
void Controller::attempted(const std::string& name,Time now) { std::lock_guard lock(mutex_); attempted_[name]=now; }
bool Controller::needs_backpack() const {
    std::lock_guard lock(mutex_);
    if (!enabled_ || !running_) return false;
    for (unsigned i=0;i<preferences_.items.size();++i) {
        const auto& rule=preferences_.items[i];
        if (rule.when==UseWhen::backpack && (!rule.limit || uses_[i]<rule.limit)) return true;
    }
    return false;
}
bool Controller::needs_reading() const {
    std::lock_guard lock(mutex_);
    if (!enabled_) return false;
    if ((preferences_.follow && !preferences_.fields.empty()) || !preferences_.boosters.empty()
        || preferences_.boost.dice_slot || preferences_.boost.glitter_slot) return true;
    for (unsigned i=0;i<preferences_.items.size();++i) {
        const auto& rule=preferences_.items[i];
        if ((rule.when==UseWhen::boosted || rule.when==UseWhen::boosted_conversion) && (!rule.limit || uses_[i]<rule.limit)) return true;
    }
    return false;
}
std::vector<extensions::Metric> Controller::metrics() const {
    std::lock_guard lock(mutex_);
    std::vector<extensions::Metric> result{{"Field",active_.empty()?"No selected field boosted":active_},
        {"Reading",readable_?"Current":"Waiting for buff bar"}};
    if (preferences_.boost.dice_slot) result.push_back({"Dice",std::to_string(dice_used_)+" / "+std::to_string(preferences_.boost.dice_limit)+" presses"});
    if (preferences_.boost.glitter_slot) result.push_back({"Glitter",std::to_string(glitter_used_)+" / "+std::to_string(preferences_.boost.glitter_limit)+" presses"});
    for (unsigned i=0;i<uses_.size();++i) if (preferences_.items[i].when!=UseWhen::off)
        result.push_back({"Slot "+std::to_string(i+1),std::to_string(uses_[i])+" presses"+(preferences_.items[i].limit?" / "+std::to_string(preferences_.items[i].limit):"")});
    return result;
}
}
