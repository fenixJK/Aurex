#include "hive/modules/combat/plan.hpp"
#include "../shared/movement.hpp"

namespace hive::features::combat {
namespace {
class Hunt final : public automation::Program {
    Enemy enemy_;
    double speed_;
    automation::ProgramFactory prepare_,home_;
    std::shared_ptr<const movement::Recipes> movement_;
    unsigned field_{},phase_{};
    std::optional<Time> began_;
public:
    Hunt(Enemy enemy,double speed,automation::ProgramFactory prepare,automation::ProgramFactory home,movement::Source source)
        :enemy_(std::move(enemy)),speed_(speed),prepare_(std::move(prepare)),home_(std::move(home)),movement_(movement::snapshot(source)) {
        if (!prepare_ || !home_ || enemy_.hunt_fields.empty() || !enemy_.progress) throw std::invalid_argument("Hunting needs fields and hive travel");
        enemy_.progress->begin(0);
    }
    automation::RunConfig config() const override { return {std::chrono::minutes(15),speed_,{},true,true}; }
    ActivityStatus activity_status() const override { return {"Searching",enemy_.species}; }
    automation::Step next(const automation::View& view) override {
        using namespace automation;
        if (!began_) {
            began_=view.context.now;
            const auto field=enemy_.requested_field?enemy_.requested_field(view.context):std::string{};
            const auto found=std::find(enemy_.hunt_fields.begin(),enemy_.hunt_fields.end(),field);
            if (found!=enemy_.hunt_fields.end()) std::rotate(enemy_.hunt_fields.begin(),found,found+1);
        }
        const bool search_ended=view.context.now-*began_>=std::chrono::minutes(5);
        if (phase_==3) {
            const auto progress=enemy_.progress->read();
            if (progress.defeated) return Done{"Defeated "+enemy_.species};
            if (progress.unavailable) return Done{enemy_.species+" was defeated by another player"};
            if (field_+1>=enemy_.hunt_fields.size() || search_ended) {
                enemy_.progress->finish(false,std::chrono::minutes(10)); return Done{"Vicious Bee search finished"};
            }
            phase_=4; return RunProgram{home_};
        }
        if (phase_==4) { ++field_; phase_=0; return RunProgram{prepare_}; }
        if (phase_==0) { phase_=1; return Navigate{enemy_.hunt_fields[field_]+".entry"}; }
        if (phase_==1) {
            auto encounter_enemy=enemy_;
            encounter_enemy.scheduled={}; encounter_enemy.field=enemy_.hunt_fields[field_];
            encounter_enemy.search=movement_walks(*movement_,"vicious-search",{{"field",enemy_.hunt_fields[field_]}},true);
            std::vector<Walk> parts;
            for (const auto& walk:encounter_enemy.search) {
                auto remaining=walk.leg.tiles;
                while (remaining>.001) { auto part=walk; part.leg.tiles=std::min(1.,remaining); parts.push_back(part); remaining-=part.leg.tiles; }
            }
            encounter_enemy.search=std::move(parts); phase_=3;
            return RunProgram{[enemy=std::move(encounter_enemy),speed=speed_,entered=view.context.now] { return encounter(enemy,speed,entered); }};
        }
        return Wait{};
    }
};
}
std::unique_ptr<automation::Program> hunt(const Enemy& enemy,double speed,automation::ProgramFactory prepare,automation::ProgramFactory home,movement::Source movement) {
    return std::make_unique<Hunt>(enemy,speed,std::move(prepare),std::move(home),std::move(movement));
}
}
