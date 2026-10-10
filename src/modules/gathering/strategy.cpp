#include "strategy.hpp"
#include "core/runtime/guards.hpp"
#include <algorithm>

namespace hive::features::gathering {
namespace {
using namespace automation;
struct Control {
    bool stopping{};
    std::string reason;
    unsigned heading{};
    std::optional<game::CameraMove> turning;
    std::function<bool()> yield;
};
class PatternGuard final : public RunGuard {
    std::unique_ptr<RunGuard> observations_;
    std::shared_ptr<Control> control_;
    Time duration_;
    TimeExtension extension_;
public:
    PatternGuard(RunConfig limits,std::shared_ptr<Control> control,TimeExtension extension)
        :observations_(observation_guard(limits)),control_(std::move(control)),duration_(limits.duration),extension_(std::move(extension)) {}
    GuardDecision evaluate(const View& view,Time started) override {
        const auto reading=observations_->evaluate(view,started);
        if (reading.state==GuardState::failed || reading.state==GuardState::waiting) return reading;
        if (!control_->stopping) {
            if (reading.state==GuardState::completed) control_->reason=reading.message;
            else if (control_->yield && control_->yield()) control_->reason="Gather interrupted";
            else {
                const bool extending=extension_.active && view.elapsed>=duration_-Time(3000) && view.elapsed<duration_+extension_.limit && extension_.active(view);
                if (view.elapsed>=duration_ && !extending) control_->reason="Time limit reached";
            }
            control_->stopping=!control_->reason.empty();
        }
        if (control_->stopping && !control_->turning) return {GuardState::completed,control_->reason};
        return {};
    }
};
class PatternStrategy final : public Program {
    std::unique_ptr<Program> source_;
    RunConfig limits_;
    TimeExtension extension_;
    std::shared_ptr<Control> control_=std::make_shared<Control>();
    std::function<void(unsigned)> completed_;
    ProgramFactory between_cycles_;
    Time corrected_{};
    bool correcting_{},using_slot_{};
    Time used_slot_{Time(-2000)};
    std::size_t source_steps_{};
    View source_view(View view) const { view.completed_steps=source_steps_; return view; }
    std::function<unsigned(const View&)> hotbar_;
public:
    PatternStrategy(ProgramFactory create,RunConfig limits,std::function<void(unsigned)> completed,ProgramFactory between_cycles,std::function<bool()> yield,std::function<unsigned(const automation::View&)> hotbar,TimeExtension extension)
        :limits_(limits),extension_(std::move(extension)),completed_(std::move(completed)),between_cycles_(std::move(between_cycles)),hotbar_(std::move(hotbar)) {
        if (!create || !completed_ || !limits.observe || !limits.backpack_limit
            || extension_.limit<Time{} || extension_.limit>std::chrono::minutes(30)
            || limits.duration<Time(1000) || limits.duration+extension_.limit>std::chrono::hours(8)
            || !std::isfinite(limits.speed) || limits.speed<1 || limits.speed>200)
            throw std::invalid_argument("Invalid gathering strategy limits");
        control_->yield=std::move(yield);
        observation_guard(limits);
        source_=create();
        if (!source_) throw std::invalid_argument("Missing gathering strategy");
    }
    WorkActivity activity() const noexcept override { return WorkActivity::gathering; }
    RunConfig config() const override {
        auto result=limits_; result.require_done=false;
        result.duration=std::min(limits_.duration+extension_.limit+Time(2500),Time(std::chrono::hours(8)));
        return result;
    }
    std::unique_ptr<RunGuard> guard() const override { return std::make_unique<PatternGuard>(limits_,control_,extension_); }
    void observe(const View& view) override { source_->observe(source_view(view)); }
    Step next(const View& view) override {
        if (control_->stopping) return Done{control_->reason};
        if (hotbar_ && view.elapsed-used_slot_>=Time(2000)) {
            const auto slot=hotbar_(view);
            if (slot>7) throw std::invalid_argument("Invalid requested hotbar slot");
            if (slot) {
                used_slot_=view.elapsed; using_slot_=true;
                return Hold{{static_cast<Key>(static_cast<int>(Key::hotbar_1)+slot-1)},Time(50)};
            }
        }
        auto step=source_->next(source_view(view));
        if (std::holds_alternative<EndCycle>(step)) {
            if (between_cycles_ && view.elapsed-corrected_>=Time(5000)) {
                correcting_=true; return RunProgram{between_cycles_};
            }
        } else if (const auto* walk=std::get_if<Walk>(&step)) {
            game::movement_slice(walk->leg,walk->leg.tiles,limits_.speed);
        } else if (const auto* hold=std::get_if<Hold>(&step)) {
            if (hold->duration<Time(1) || hold->duration>Time(10000) || hold->keys.size()>3)
                throw std::invalid_argument("Invalid pattern key hold");
            for (const auto key : hold->keys) if ((key<Key::forward || key>Key::jump) && key!=Key::primary)
                throw std::invalid_argument("Pattern turns must use camera operations");
        } else if (const auto* wait=std::get_if<Wait>(&step)) {
            if (wait->duration<Time(1) || wait->duration>Time(1000)) throw std::invalid_argument("Invalid pattern wait");
        } else if (const auto* camera=std::get_if<MoveCamera>(&step)) {
            game::camera_actions(camera->movement);
            if (camera->movement.key!=Key::rotate_left && camera->movement.key!=Key::rotate_right)
                throw std::invalid_argument("Patterns can turn facing but cannot change zoom or pitch");
            auto turn=game::heading_move(0,game::turned_heading(0,camera->movement));
            if (!turn) return Wait{Time(1),"Facing unchanged"};
            turn->press=camera->movement.press; turn->release=camera->movement.release;
            control_->turning=*turn; return MoveCamera{*turn};
        } else if (!std::holds_alternative<Done>(step)) {
            throw std::invalid_argument("Gathering patterns can only move, turn, wait, end a cycle or finish");
        }
        return step;
    }
    void step_completed(const View& view) override {
        if (using_slot_) { using_slot_=false; return; }
        if (correcting_) { corrected_=view.elapsed; correcting_=false; }
        if (control_->turning) {
            control_->heading=game::turned_heading(control_->heading,*control_->turning); control_->turning.reset();
        }
        ++source_steps_; source_->step_completed(source_view(view));
    }
    void finished(State state) override {
        source_->finished(state);
        if (state==State::completed) {
            if (control_->turning) throw std::runtime_error("Pattern stopped during an unconfirmed camera turn");
            completed_(control_->heading);
        }
    }
};
}
std::unique_ptr<automation::Program> bounded_pattern(automation::ProgramFactory create,
    automation::RunConfig limits,std::function<void(unsigned)> completed,automation::ProgramFactory between_cycles,std::function<bool()> yield,std::function<unsigned(const automation::View&)> hotbar,TimeExtension extension) {
    return std::make_unique<PatternStrategy>(std::move(create),limits,std::move(completed),std::move(between_cycles),std::move(yield),std::move(hotbar),std::move(extension));
}
}
