#include "hive/modules/planters/actions.hpp"

namespace hive::features::planters {
namespace {
class FieldInventory final : public game::InventorySource {
    std::shared_ptr<const game::InventorySource> inventory_;
    std::string target_;
public:
    FieldInventory(std::shared_ptr<const game::InventorySource> inventory,std::string target)
        :inventory_(std::move(inventory)),target_(std::move(target)) {}
    Observation<game::InventoryView> read(const ObservationContext& c,const game::State& state) const override {
        auto result=inventory_->read(c,state);
        if (!result.has_value(c,Time(2000))) return {};
        result.value.target=target_;
        result.value.drop=Point{.5,.5};
        if (result.value.open) {
            const auto& r=result.value.viewport;
            if (r.x<=.5 && r.y<=.5 && r.x+r.width>=.5 && r.y+r.height>=.5) result.value.drop.reset();
        }
        return result;
    }
};
class ObservedActions final : public Actions {
    std::map<std::string,PlanterControls> controls_;
    std::map<std::string,FieldAccess> fields_;
    std::shared_ptr<const game::PlanterSource> observations_;
public:
    ObservedActions(std::map<std::string,PlanterControls> controls,std::map<std::string,FieldAccess> fields,std::shared_ptr<const game::PlanterSource> observations)
        :controls_(std::move(controls)),fields_(std::move(fields)),observations_(std::move(observations)) {
        if (controls_.empty() || controls_.size()>32 || fields_.empty() || fields_.size()>32)
            throw std::invalid_argument("Planter controls and field inventory access are required");
        for (const auto& [name,control] : controls_) {
            if (name.empty() || name.size()>80) throw std::invalid_argument("Invalid planter control identity");
            game::validate(control.place_question); game::validate(control.harvest_question); game::validate(control.harvest_prompt);
            for (const auto& [field,inventory] : fields_) {
                if (field.empty() || field.size()>80 || !inventory.source) throw std::invalid_argument("Invalid planter inventory destination");
                workflows::validate(workflows::InventoryDragPlan{control.inventory_names,inventory.target});
                if (inventory.harvest_approach) {
                    if (!observations_)
                        throw std::invalid_argument("Planter approach needs observed proximity");
                    game::planter_harvest_prompt(name);
                }
            }
        }
    }
    std::optional<automation::ApproachLandmark> locate(const Stats& planter) const override {
        const auto field=fields_.find(planter.field);
        if (!controls_.contains(planter.planter) || field==fields_.end())
            throw std::runtime_error("Planter interaction is not connected for this equipment and field");
        if (!field->second.harvest_approach) return {};
        auto approach=field->second.harvest_approach();
        workflows::validate(approach);
        if (approach.completion!=workflows::CheckpointCompletion::observation) throw std::invalid_argument("Planter approach needs observed proximity");
        return automation::ApproachLandmark{std::move(approach),game::planter_proximity(observations_,planter.planter)};
    }
    std::optional<automation::InspectInventoryItem> inspect(const Stats& planter) const override {
        const auto control=controls_.find(planter.planter); const auto field=fields_.find(planter.field);
        if (control==controls_.end() || field==fields_.end()) return {};
        return automation::InspectInventoryItem{control->second.inventory_names,field->second.source,
            std::make_shared<Observation<workflows::InventoryMatch>>(),false,true};
    }
    std::vector<automation::Step> prepare(Operation operation,const Stats& planter) const override {
        const auto it=controls_.find(planter.planter);
        const auto field=fields_.find(planter.field);
        if (it==controls_.end() || field==fields_.end()) throw std::runtime_error("Planter interaction is not connected for this equipment and field");
        const auto& control=it->second;
        if (operation==Operation::place) {
            workflows::InventoryDragPlan search{control.inventory_names,field->second.target}; search.start_at_bottom=true;
            search.use_recent_view=true;
            std::vector<automation::Step> steps{automation::DragInventoryItem{std::move(search),field->second.source,
                    std::make_shared<Observation<workflows::InventoryMatch>>()},
                automation::AnswerDialog{{control.place_question,workflows::DialogChoice::yes,Time(10000),false}}};
            return steps;
        }
        if (operation==Operation::harvest) {
            std::vector<automation::Step> steps{automation::Hold{{Key::interact},Time(100)}};
            steps.push_back(automation::AnswerDialog{{control.harvest_question,workflows::DialogChoice::yes,Time(10000),false}});
            return steps;
        }
        throw std::invalid_argument("Unknown planter operation");
    }
};
}
std::shared_ptr<const Actions> observed_actions(std::map<std::string,PlanterControls> controls,std::map<std::string,FieldAccess> fields,
    std::shared_ptr<const game::PlanterSource> observations) {
    return std::make_shared<ObservedActions>(std::move(controls),std::move(fields),std::move(observations));
}
std::shared_ptr<const Actions> desktop_actions(const ExecutionPlan& plan,
    std::shared_ptr<const game::InventorySource> inventory,std::shared_ptr<const game::PlanterSource> observations) {
    if (!inventory || !observations || plan.destinations.empty())
        throw std::invalid_argument("Desktop planters need inventory, observations and routed fields");
    std::map<std::string,PlanterControls> controls;
    for (const auto& item : equipment()) {
        const auto name=item.label+(item.id=="PlanterOfPlenty"?"":" Planter");
        auto question=[&](const std::string& verb) {
            game::PromptLandmark result{{},{.1,.15,.8,.7},.8f,game::PromptMatch::phrase};
            for (const auto* article : {"", "a ", "the ", "your "}) result.phrases.push_back(verb+" "+article+name);
            return result;
        };
        controls.emplace(item.id,PlanterControls{{name},question("Place"),game::planter_harvest_prompt(item.id),question("Harvest")});
    }
    const auto search=[source=plan.movement,speed=plan.config.speed] {
        return movement::snapshot(source)->path("planter-search",{},speed).at(0);
    };
    std::map<std::string,FieldAccess> fields;
    for (const auto& [field,destination] : plan.destinations)
        fields.emplace(field,FieldAccess{destination,std::make_shared<FieldInventory>(inventory,destination),search});
    return observed_actions(std::move(controls),std::move(fields),std::move(observations));
}
}
