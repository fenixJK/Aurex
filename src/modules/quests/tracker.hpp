#pragma once
#include "core/module_host/requests.hpp"
#include "game/detection/blooms.hpp"

namespace hive::features::quests {
enum class Kind { field_pollen, red_pollen, blue_pollen, white_pollen, any_pollen, enemy, field_activity, gather_activity, any_enemy, feeding, challenge, station, petals, tracked, unsupported };
struct Objective {
    std::string id, text, field;
    Kind kind{Kind::unsupported};
    std::uint64_t collected{}, required{};
    bool known{};
    std::string enemy;
    bool visual{};
    std::string target;
    game::PetalColor petal_color{game::PetalColor::any};
    bool goo{};
};
struct Card {
    std::string giver, quest;
    std::vector<Objective> objectives;
    bool complete_view{};
};
struct Field { std::string name; std::set<Kind> pollen; };
struct Report {
    game::Requests requests;
    std::vector<game::FieldRequest> completed_fields;
    std::vector<std::string> ready, blocked;
    std::vector<std::pair<std::string,Objective>> actions;
};
class Tracker {
public:
    explicit Tracker(std::vector<Field> fields,Time freshness=Time(30000),int priority=40,Time retention=Time{});
    bool observe(const ObservationContext& context,const Observation<Card>& card);
    Report report(const ObservationContext& context,bool retained_only=false);
    const std::map<std::string,Observation<Card>>& observed_cards() const { return cards_; }
    void reset();
private:
    std::vector<Field> fields_;
    std::map<std::string,Observation<Card>> cards_;
    struct Remembered {
        std::string quest;
        std::map<std::string,Observation<Objective>> rows;
    };
    std::map<std::string,Remembered> retained_;
    Time freshness_,retention_, last_{};
    int priority_;
    std::uint64_t generation_{}, revision_{};
    Window window_;
    void context(const ObservationContext& context);
};
}
