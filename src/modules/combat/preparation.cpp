#include "plan.hpp"
#include "game/detection/buffs.hpp"

namespace hive::features::combat {
namespace {
class Preparation final : public automation::Program {
    std::string location_,buff_;
    std::shared_ptr<const game::DetectionSource> source_;
    std::function<Time()> clock_;
    Time deadline_{};
    double speed_;
    bool arrived_{},ready_{};
    std::vector<workflows::CheckpointPlan> entry_;
    std::size_t next_entry_{};
public:
    Preparation(std::string location,std::string buff,std::shared_ptr<const game::DetectionSource> source,double speed,std::vector<workflows::CheckpointPlan> entry)
        :location_(std::move(location)),buff_(std::move(buff)),source_(std::move(source)),speed_(speed),entry_(std::move(entry)) {
        if (!source_) throw std::invalid_argument("Buff preparation needs a detection source");
    }
    Preparation(std::string location,Time deadline,std::function<Time()> clock,double speed)
        :location_(std::move(location)),clock_(std::move(clock)),deadline_(deadline),speed_(speed) {}
    automation::RunConfig config() const override { return {std::chrono::minutes(10),speed_,{},true,true}; }
    ActivityStatus activity_status() const override { return {"Waiting",buff_.empty()?"Mondo spawn":buff_}; }
    automation::Step next(const automation::View& view) override {
        using namespace automation;
        if (!arrived_) { arrived_=true; return Navigate{location_}; }
        if (clock_) return clock_()>=deadline_?Step(Done{"Spawn time reached"}):Step(Wait{Time(250),"Waiting for Mondo spawn"});
        if (ready_) return next_entry_<entry_.size()?Step(FollowPath{entry_[next_entry_++]}):Step(Done{"Boss approach complete"});
        if (!deadline_.count()) deadline_=view.context.now+Time(30000);
        if (const auto packet=source_->read(); packet && packet->error.empty()
            && Observation<bool>{Knowledge::present,true,packet->context.now,packet->context.window,packet->context.generation}.has_value(view.context,Time(7000)))
            if (const auto buffs=std::dynamic_pointer_cast<const game::BuffDetection>(packet->value))
                for (const auto& buff:buffs->buffs) if (buff.name==buff_ && buff.active.value_or(false)) ready_=true;
        if (view.context.now>=deadline_) ready_=true;
        if (ready_) return next(view);
        return Wait{Time(250),"Waiting for "+buff_};
    }
};
}
std::unique_ptr<automation::Program> wait_for_buff(std::string location,std::string buff,std::shared_ptr<const game::DetectionSource> source,double speed,std::vector<workflows::CheckpointPlan> entry) {
    return std::make_unique<Preparation>(std::move(location),std::move(buff),std::move(source),speed,std::move(entry));
}
std::unique_ptr<automation::Program> wait_for_spawn(std::string location,Time deadline,std::function<Time()> clock,double speed) {
    return std::make_unique<Preparation>(std::move(location),deadline,std::move(clock),speed);
}
}
