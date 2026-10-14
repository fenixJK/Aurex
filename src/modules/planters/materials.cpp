#include "materials.hpp"
#include "drops.hpp"
#include <tuple>
#include <algorithm>

namespace hive::features::planters {
std::span<const MaterialRoute> material_routes() {
    static const MaterialRoute routes[]{
        {"glue","Glue","CandyPlanter",{"Stump","Stump","Stump"},10,std::chrono::days(7),"glue"},
        {"blue-extract","Blue Extract","HydroponicPlanter",{"Blue Flower","Bamboo","Pine Tree","Stump"},100,std::chrono::days(36),"blue-extract"},
        {"oil","Oil","HydroponicPlanter",{"Sunflower","Sunflower","Sunflower"},100,std::chrono::days(36),"oil"},
        {"red-extract","Red Extract","HeatTreatedPlanter",{"Mushroom","Strawberry","Rose","Pepper"},100,std::chrono::days(36),"red-extract"},
        {"enzymes","Enzymes","HeatTreatedPlanter",{"Pineapple","Pineapple","Pineapple"},100,std::chrono::days(36),"enzymes"},
        {"neonberry","Neonberry","PesticidePlanter",{"Stump","Mountain Top","Coconut"},25,std::chrono::days(7),"neonberry"},
        {"stinger","Stinger","RedClayPlanter",{"Clover","Spider","Cactus"},10,std::chrono::days(14),"stinger"},
        {"super-smoothie","Super Smoothie","PlanterOfPlenty",{"Bamboo"},10,std::chrono::days(36),"super-smoothie"},
        {"loaded-heat","Loaded Dice","HeatTreatedPlanter",{"Mushroom"},1,std::chrono::days(1),"loaded-dice"},
        {"loaded-hydro","Loaded Dice","HydroponicPlanter",{"Blue Flower"},1,std::chrono::days(1),"loaded-dice"},
        {"loaded-tacky","Loaded Dice","TackyPlanter",{"Mountain Top"},1,std::chrono::days(36),"loaded-dice"},
        {"swirled-plastic","Swirled Wax","PlasticPlanter",{"Coconut"},1,std::chrono::days(36),"swirled-wax"},
        {"swirled-candy","Swirled Wax","CandyPlanter",{"Cactus"},1,std::chrono::days(36),"swirled-wax"},
        {"swirled-blue","Swirled Wax","BlueClayPlanter",{"Spider"},1,std::chrono::days(36),"swirled-wax"},
        {"swirled-red","Swirled Wax","RedClayPlanter",{"Sunflower"},1,std::chrono::days(36),"swirled-wax"},
        {"swirled-tacky","Swirled Wax","TackyPlanter",{"Pumpkin"},1,std::chrono::days(36),"swirled-wax"},
        {"swirled-pesticide","Swirled Wax","PesticidePlanter",{"Strawberry"},1,std::chrono::days(36),"swirled-wax"},
        {"swirled-heat","Swirled Wax","HeatTreatedPlanter",{"Blue Flower"},1,std::chrono::days(36),"swirled-wax"},
        {"swirled-hydro","Swirled Wax","HydroponicPlanter",{"Mushroom"},1,std::chrono::days(36),"swirled-wax"},
        {"swirled-petal","Swirled Wax","PetalPlanter",{"Strawberry"},1,std::chrono::days(36),"swirled-wax"},
        {"caustic-plastic","Caustic Wax","PlasticPlanter",{"Rose"},1,std::chrono::days(36),"caustic-wax"},
        {"caustic-candy","Caustic Wax","CandyPlanter",{"Mountain Top"},1,std::chrono::days(36),"caustic-wax"},
        {"caustic-blue","Caustic Wax","BlueClayPlanter",{"Clover"},1,std::chrono::days(36),"caustic-wax"},
        {"caustic-red","Caustic Wax","RedClayPlanter",{"Clover"},1,std::chrono::days(36),"caustic-wax"},
        {"caustic-tacky","Caustic Wax","TackyPlanter",{"Bamboo"},1,std::chrono::days(36),"caustic-wax"},
        {"caustic-heat","Caustic Wax","HeatTreatedPlanter",{"Coconut"},1,std::chrono::days(36),"caustic-wax"},
        {"caustic-hydro","Caustic Wax","HydroponicPlanter",{"Coconut"},1,std::chrono::days(36),"caustic-wax"},
        {"caustic-petal","Caustic Wax","PetalPlanter",{"Dandelion"},1,std::chrono::days(36),"caustic-wax"},
        {"dapper-sticker","Dapper From Above","TackyPlanter",{"Clover","Coconut","Stump","Pepper","Mountain Top"},1,Time{},"dapper-sticker",true}};
    return routes;
}
std::string material_goal(const std::string& intent) {
    if(intent.starts_with("item:")) {
        const auto id=intent.substr(5);
        for(const auto& goal:item_goals()) if(goal.id==id) return id;
    }
    for(const auto& route:material_routes()) if(route.id==intent) return route.goal;
    return {};
}
bool valid_material(const std::string& intent,const Stats& stats) {
    if(intent.starts_with("item:")) return !material_goal(intent).empty() && item_source(material_goal(intent),stats).preference>0;
    for(const auto& route:material_routes()) if(route.id==intent) return route.planter==stats.planter
        && std::find(route.fields.begin(),route.fields.end(),stats.field)!=route.fields.end();
    return false;
}
void validate_materials(const MaterialHistory& history) {
    if(history.size()>material_routes().size()+item_goals().size()) throw std::invalid_argument("Too many material records");
    for(const auto& [id,progress]:history) {
        if(progress.ready<Time{}) throw std::invalid_argument("Invalid material time");
        if(id.starts_with("item:") && !material_goal(id).empty() && !progress.step && !progress.complete) continue;
        const auto routes=material_routes();
        const auto found=std::find_if(routes.begin(),routes.end(),[&](const auto& route) {return route.id==id;});
        if(found==routes.end() || progress.step>=found->fields.size() || (progress.complete && (!found->once || progress.step)))
            throw std::invalid_argument("Invalid material goal progress");
    }
}
void material_harvest(MaterialHistory& history,const Stats& stats,bool full,Time now,const std::string& intent) {
    validate_materials(history);
    if(now<Time{} || now>Time::max()-std::chrono::days(36)) throw std::invalid_argument("Invalid material harvest time");
    const auto goal=material_goal(intent);
    if(full && !goal.empty()) history["item:"+goal]={0,now};
    for(const auto& route:material_routes()) if(route.planter==stats.planter) {
        auto& progress=history[route.id];
        if(now<progress.ready || progress.complete) continue;
        if(full && route.fields[progress.step]==stats.field) ++progress.step;
        else progress.step=full && route.fields.front()==stats.field?1u:0u;
        if(progress.step==route.fields.size()) {progress.step=0;progress.ready=now+route.cooldown;progress.complete=route.once;}
    }
}
std::vector<Assignment> material_choices(std::span<const Group> groups,std::span<const Active> occupied,
    std::size_t capacity,Selection selection,const MaterialPlan& plan) {
    if(!plan.farms_items() || plan.goals.empty() || occupied.size()>=capacity) return {};
    validate_materials(plan.history);
    if(plan.mode==Mode::nectar) for(const auto& group:groups)
        if(group.target.minimum>0 && (!group.current_known || group.current<bounds(group.target).lower)) return {};
    const auto progress_for=[&](const std::string& id) {
        const auto found=plan.history.find(id);return found==plan.history.end()?MaterialProgress{}:found->second;
    };
    const auto selected=[&](const std::string& goal) {return std::find(plan.goals.begin(),plan.goals.end(),goal)!=plan.goals.end();};
    std::map<std::string,std::string> locked;
    for(const auto& route:material_routes()) if(selected(route.goal) && !plan.excluded_routes.contains(route.id)
        && progress_for(route.id).step && !locked.contains(route.planter)) locked[route.planter]=route.id;
    struct Offer {const Candidate* candidate;std::string intent,goal;int tier;ItemSource source;};
    std::vector<Offer> offers;
    for(const auto& route:material_routes()) if(selected(route.goal) && !plan.excluded_routes.contains(route.id)) {
        const auto progress=progress_for(route.id);
        if(progress.complete || plan.now<progress.ready || (locked.contains(route.planter) && locked.at(route.planter)!=route.id)) continue;
        for(const auto& group:groups) for(const auto& c:group.candidates)
            if(c.stats.planter==route.planter && c.stats.field==route.fields[progress.step])
                offers.push_back({&c,route.id,route.goal,progress.step?2:1,{}});
    }
    for(const auto& goal:plan.goals) for(const auto& group:groups) for(const auto& c:group.candidates) {
        if(locked.contains(c.stats.planter)) continue;
        const auto source=item_source(goal,c.stats);
        if(source.preference) offers.push_back({&c,"item:"+goal,goal,0,source});
    }
    std::vector<Active> busy(occupied.begin(),occupied.end());
    std::vector<Assignment> items;
    const auto served=[&](const std::string& goal) {return std::count_if(busy.begin(),busy.end(),[&](const auto& active) {return material_goal(active.material)==goal;});};
    const auto busy_at=[&](const Stats& stats) {return std::any_of(busy.begin(),busy.end(),[&](const auto& p) {return p.planter==stats.planter || p.field==stats.field;});};
    bool changed=true;
    std::set<std::pair<std::string,std::string>> unsafe;
    while(busy.size()<capacity && !offers.empty()) {
        if(changed) std::stable_sort(offers.begin(),offers.end(),[&](const auto& a,const auto& b) {
            const auto rank=[&](const auto& offer) {
                const auto& c=*offer.candidate;
                return std::tuple{-offer.tier,served(offer.goal),progress_for("item:"+offer.goal).ready,
                    std::find(plan.goals.begin(),plan.goals.end(),offer.goal)-plan.goals.begin(),-offer.source.preference,
                    !offer.source.field_bonus,-double(offer.source.minimum)/(c.stats.full_seconds+2*c.travel.seconds),
                    c.stats.full_seconds+2*c.travel.seconds,c.last_used};
            };
            return rank(a)>rank(b);
        });
        changed=false;const auto offer=offers.back();offers.pop_back();const auto& c=*offer.candidate;
        if(busy_at(c.stats) || unsafe.contains({c.stats.planter,c.stats.field})) continue;
        const Assignment item{c.stats,Phase::build,c.stats.full_seconds,c.stats.full_seconds+c.travel.seconds,0,offer.intent};
        if(plan.mode==Mode::items) {items.push_back(item);busy.push_back({c.stats.planter,c.stats.field,item.material});changed=true;continue;}
        auto reserved=busy;reserved.push_back({c.stats.planter,c.stats.field,item.material});
        auto choices=assign(groups,reserved,capacity,selection);choices.insert(choices.begin(),item);
        const double horizon=c.stats.full_seconds+2*c.travel.seconds;
        bool safe=true;
        for(const auto& group:groups) if(group.target.minimum>0) {
            auto events=group.pending;double outbound=0;
            for(const auto& choice:choices) {
                const auto extra=std::max(0.,choice.ready_in-choice.growth);outbound+=extra;
                if(choice.stats.nectar==group.nectar) events.push_back({outbound+choice.growth+extra,choice.stats.rate()*choice.growth});
            }
            std::sort(events.begin(),events.end(),[](const auto& a,const auto& b) {return a.seconds<b.seconds;});
            double value=group.current,last=0;
            for(const auto& event:events) {
                if(event.seconds>horizon) break;
                value-=std::max(0.,event.seconds-last)/864.;
                if(value<bounds(group.target).lower) {safe=false;break;}
                value=std::min(100.,value+event.amount);last=event.seconds;
            }
            if(value-(horizon-last)/864.<bounds(group.target).lower) safe=false;
            if(!safe) break;
        }
        if(safe) return choices;
        unsafe.insert({c.stats.planter,c.stats.field});
    }
    return items;
}
std::vector<Assignment> placement_choices(std::span<const Group> groups,std::span<const Active> occupied,
    std::size_t capacity,Selection selection,const MaterialPlan& plan,bool at_destination) {
    auto choices=material_choices(groups,occupied,capacity,selection,plan);
    if(plan.mode==Mode::items && plan.secondary && choices.size()+occupied.size()<capacity) {
        auto remaining=std::vector<Group>(groups.begin(),groups.end());
        auto busy=std::vector<Active>(occupied.begin(),occupied.end());
        std::set<std::string> chained;
        for(const auto& route:material_routes()) if(!plan.excluded_routes.contains(route.id)
            && std::find(plan.goals.begin(),plan.goals.end(),route.goal)!=plan.goals.end()) {
            const auto progress=plan.history.find(route.id);
            if(progress!=plan.history.end() && progress->second.step) chained.insert(route.planter);
        }
        for(auto& group:remaining) {
            if(!group.current_known) group.target.minimum=0;
            std::erase_if(group.candidates,[&](const auto& candidate) {return chained.contains(candidate.stats.planter);});
        }
        double outbound=0;
        for(const auto& item:choices) {
            busy.push_back({item.stats.planter,item.stats.field,item.material});
            const auto travel=std::max(0.,item.ready_in-item.growth);outbound+=travel;
            for(auto& group:remaining) if(group.nectar==item.stats.nectar)
                group.pending.push_back({outbound+item.growth+travel,item.stats.rate()*item.growth});
        }
        const auto nectar=assign(remaining,busy,capacity,selection,at_destination);
        choices.insert(choices.end(),nectar.begin(),nectar.end());
    }
    if (choices.empty() && plan.mode==Mode::nectar) choices=assign(groups,occupied,capacity,selection,at_destination);
    return choices;
}
std::optional<Time> next_material_ready(const MaterialPlan& plan,Time after) {
    if(!plan.farms_items()) return {};
    std::optional<Time> next;
    for(const auto& route:material_routes()) if(!plan.excluded_routes.contains(route.id)
        && std::find(plan.goals.begin(),plan.goals.end(),route.goal)!=plan.goals.end()) {
        const auto found=plan.history.find(route.id);
        if(found!=plan.history.end() && !found->second.complete && found->second.ready>after && (!next || found->second.ready<*next)) next=found->second.ready;
    }
    return next;
}
}
