#include "actions.hpp"
#include "catalog.hpp"
#include "game/actions/inventory/feeding.hpp"
#include "game/actions/activities/ant_challenge.hpp"
#include "game/actions/field/petals.hpp"
#include "game/data/fields.hpp"
#include <algorithm>

namespace hive::features::quests {
void configure_actions(CyclePlan& plan,const game::RouteNetwork& routes,
    std::shared_ptr<const game::InventorySource> inventory,std::shared_ptr<const game::DetectionSource> hive_view,
    std::shared_ptr<const game::LandmarkSource> reference,std::shared_ptr<const game::DetectionSource> blooms,movement::Source movement,std::shared_ptr<game::Cooldowns> collection_cooldowns,
    std::function<automation::ProgramFactory(const std::string&)> collect_station,std::shared_ptr<const BrowserSource> quests) {
    if (inventory && hive_view && reference)
        for (const auto* food:{"Strawberry","Blueberry"}) {
            automation::FeedingPlan feeding{food,100,plan.prepare,inventory,hive_view,reference};
            plan.actions.emplace(food,features::quests::QuestAction{[feeding](const features::quests::Objective& objective) mutable {
                auto feed=feeding;
                if (!objective.visual) feed.amount=unsigned(std::min<std::uint64_t>(100,objective.required-objective.collected));
                return automation::feed_bees(feed);
            },{}});
        }

    const auto pass=collect_station?collect_station("Free Ant Pass Dispenser"):automation::ProgramFactory{};
    if (pass) plan.actions.emplace("Ant",features::quests::QuestAction{
        [prepare=plan.prepare,return_to_hive=plan.return_to_hive,speed=plan.speed,movement,collection_cooldowns](const features::quests::Objective&) {
            return automation::ant_challenge(prepare,return_to_hive,speed,movement,[collection_cooldowns](const ObservationContext& context) {
                if (!collection_cooldowns) return;
                collection_cooldowns->load();
                const auto delay=collection_cooldowns->remaining("Free Ant Pass Dispenser",std::chrono::hours(2));
                collection_cooldowns->remember("Ant Challenge",context.now,context.now+std::max(Time(60000),delay),false,context);
            });
        },[collection_cooldowns] {
            if (!collection_cooldowns) return true;
            collection_cooldowns->load();
            return collection_cooldowns->remaining("Ant Challenge",std::chrono::hours(2))==Time{};
        },pass});
    if (blooms) for (const auto& quest:features::quests::quest_catalog())
        for (const auto& row:quest.rows) {
            if (row.action!="petals") continue;
            const auto field=std::find_if(game::fields().begin(),game::fields().end(),[&](const auto& value) { return value.name==row.field; });
            if (field==game::fields().end() || !routes.contains(field->location())) continue;
            automation::PetalPlan petals{field->location(),row.color,blooms,plan.prepare,plan.return_to_hive,plan.speed};
            petals.movement=movement;
            if (quests) petals.complete=[quests,giver=quest.giver,target=row.target](const automation::View& view) {
                const auto scene=quests->read();
                if (!scene.has_value(view.context,Time(2000))) return false;
                for (const auto& card:scene.value.cards) {
                    if (card.value.giver!=giver || !card.has_value(view.context,Time(2000))) continue;
                    for (const auto& objective:card.value.objectives)
                        if (objective.target==target && objective.known && objective.required && objective.collected>=objective.required) return true;
                }
                return false;
            };
            plan.actions.emplace(row.target,features::quests::QuestAction{
                [petals](const features::quests::Objective&) { return automation::collect_petals(petals); },{},
                quests?automation::ProgramFactory([quests,giver=quest.giver] { return browser_program(quests,std::make_shared<ScanResults>(),false,{giver}); }):automation::ProgramFactory{}});
        }
}
}
