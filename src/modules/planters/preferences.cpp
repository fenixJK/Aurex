#include "preferences.hpp"
#include "drops.hpp"
#include <algorithm>

namespace hive::features::planters {
std::span<const Equipment> equipment() {
    static const Equipment values[]{
        {"PlasticPlanter","Plastic"},{"CandyPlanter","Candy"},{"BlueClayPlanter","Blue Clay"},
        {"RedClayPlanter","Red Clay"},{"TackyPlanter","Tacky"},{"PesticidePlanter","Pesticide"},
        {"HeatTreatedPlanter","Heat-Treated"},{"HydroponicPlanter","Hydroponic"},{"PetalPlanter","Petal"},
        {"PlanterOfPlenty","Planter of Plenty"},{"PaperPlanter","Paper"},{"TicketPlanter","Ticket"},{"StickerPlanter","Sticker"},{"FestivePlanter","Festive"}};
    return values;
}
std::span<const std::string> nectar_names() {
    static const std::string values[]{"Comforting","Refreshing","Satisfying","Motivating","Invigorating"}; return values;
}
Preferences default_preferences() {
    Preferences result;
    for (std::size_t i=0;i<6;++i) result.planters.insert(equipment()[i].id);
    for (const auto& stats : catalog()) result.fields.insert(stats.field);
    return result;
}
void validate(const Preferences& preferences) {
    if (preferences.mode!=Mode::nectar && preferences.mode!=Mode::items) throw std::invalid_argument("Unknown planter mode");
    if (preferences.materials.size()>item_goals().size() || std::set<std::string>(preferences.materials.begin(),preferences.materials.end()).size()!=preferences.materials.size())
        throw std::invalid_argument("Invalid material goal selection");
    for (const auto& id:preferences.materials) if (std::none_of(item_goals().begin(),item_goals().end(),[&](const auto& item) { return item.id==id; }))
        throw std::invalid_argument("Unknown material goal");
    for (const auto& name : preferences.planters)
        if (std::none_of(equipment().begin(),equipment().end(),[&](const auto& item) { return item.id==name; }))
            throw std::invalid_argument("Unknown planter equipment");
    for (const auto& name : preferences.fields)
        if (std::none_of(catalog().begin(),catalog().end(),[&](const auto& item) { return item.field==name; }))
            throw std::invalid_argument("Unknown planter field");
    for (std::size_t i=0;i<5;++i) {
        bounds(preferences.targets[i]);
        if (preferences.priorities[i]<0 || preferences.priorities[i]>100) throw std::invalid_argument("Invalid nectar priority");
    }
}
void apply_preferences(std::vector<Group>& groups,const Preferences& preferences) {
    validate(preferences);
    for (auto& group : groups) {
        const auto index=static_cast<std::size_t>(group.nectar);
        if (index>=5) throw std::invalid_argument("Unknown nectar reading");
        group.target=preferences.targets[index]; group.priority=preferences.priorities[index];
        group.phase=group.current_known?phase(group.phase,group.current,group.target):Phase::build;
        std::erase_if(group.candidates,[&](const auto& candidate) {
            return !preferences.planters.contains(candidate.stats.planter) || !preferences.fields.contains(candidate.stats.field);
        });
    }
}
void apply_preferences(MaterialPlan& plan,const Preferences& preferences) {
    plan.mode=preferences.mode; plan.secondary=preferences.secondary; plan.goals=preferences.materials;
    plan.excluded_routes.clear();
    for(const auto& route:material_routes()) if(!preferences.planters.contains(route.planter)
        || std::any_of(route.fields.begin(),route.fields.end(),[&](const auto& field) {return !preferences.fields.contains(field);}))
        plan.excluded_routes.insert(route.id);
}
std::vector<Group> candidate_groups() {
    std::vector<Group> groups;
    for (unsigned i=0;i<5;++i) {
        Group group; group.nectar=static_cast<Nectar>(i); group.current_known=false;
        for (const auto& stats:catalog()) if (stats.nectar==group.nectar) group.candidates.push_back({stats,{}});
        groups.push_back(std::move(group));
    }
    return groups;
}
}
