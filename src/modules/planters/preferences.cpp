#include "preferences.hpp"
#include <algorithm>

namespace hive::features::planters {
std::span<const Equipment> equipment() {
    static const Equipment values[]{
        {"PlasticPlanter","Plastic"},{"CandyPlanter","Candy"},{"BlueClayPlanter","Blue Clay"},
        {"RedClayPlanter","Red Clay"},{"TackyPlanter","Tacky"},{"PesticidePlanter","Pesticide"},
        {"HeatTreatedPlanter","Heat-Treated"},{"HydroponicPlanter","Hydroponic"},{"PetalPlanter","Petal"},
        {"PlanterOfPlenty","Planter of Plenty"},{"PaperPlanter","Paper"},{"TicketPlanter","Ticket"}};
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
}
