#include "hive_setup.hpp"
#include "game/actions/field/shift_lock.hpp"

namespace hive::automation {
std::optional<HiveSetupSnapshot> HiveSetupResult::read() const { std::lock_guard lock(mutex_); return value_; }
std::optional<HiveSetupSnapshot> HiveSetupResult::read(const ObservationContext& c,const game::State* state) const {
    auto value=read();
    if (!value || !c.generation || !Observation<bool>{Knowledge::present,true,value->context.now,
        value->context.window,value->context.generation}.has_value(c,Time::max())) return {};
    // Respawn preserves hive ownership, but invalidates any earlier camera alignment.
    if (state && state->last_death.has_value(c,Time::max(),value->context.now) && state->last_death.value)
        value->camera_prepared=false;
    return value;
}
void HiveSetupResult::reset() { std::lock_guard lock(mutex_); value_.reset(); joined_.reset(); }
void HiveSetupResult::joined(const Window& window,Time now) {
    std::lock_guard lock(mutex_); value_.reset(); joined_=std::pair{window,now};
}
void HiveSetupResult::clear_join() { std::lock_guard lock(mutex_); joined_.reset(); }
bool HiveSetupResult::consume_join(const ObservationContext& c) {
    std::lock_guard lock(mutex_);
    const auto joined=std::exchange(joined_,std::nullopt);
    return joined && c.generation && c.window.id && c.window.id==joined->first.id
        && c.window.process==joined->first.process && c.now>=joined->second && c.now-joined->second<Time(120000);
}
void HiveSetupResult::invalidate_camera() { std::lock_guard lock(mutex_); if (value_) value_->camera_prepared=false; }
void HiveSetupResult::begin_preparation(const ObservationContext& c) {
    std::lock_guard lock(mutex_);
    if (!value_) return;
    const auto& previous=value_->context;
    if (!c.generation || !c.window.id || !c.window.bounds.valid() || c.now<previous.now
        || c.generation!=previous.generation || c.window.id!=previous.window.id
        || c.window.process!=previous.window.process || c.window.bounds!=previous.window.bounds) value_.reset();
    else value_->camera_prepared=false;
}
void HiveSetupResult::claimed(unsigned slot,const ObservationContext& c) {
    if (slot<1 || slot>6 || !c.generation || !c.window.id || !c.window.bounds.valid() || c.now<Time{})
        throw std::invalid_argument("Invalid confirmed hive claim result");
    std::lock_guard lock(mutex_); value_=HiveSetupSnapshot{slot,false,c};
}
void HiveSetupResult::prepared(const ObservationContext& c) {
    if (!c.generation || !c.window.id || !c.window.bounds.valid() || c.now<Time{}) throw std::invalid_argument("Invalid hive preparation result");
    std::lock_guard lock(mutex_);
    const bool same=value_ && value_->context.generation==c.generation && value_->context.window.id==c.window.id
        && value_->context.window.process==c.window.process && value_->context.window.bounds==c.window.bounds
        && value_->context.now<=c.now;
    value_=HiveSetupSnapshot{same?value_->claimed_slot:std::nullopt,true,c};
}
ApproachPrompt hive_row_approach(double speed,movement::Source source) {
    if (!std::isfinite(speed) || speed<8 || speed>200) throw std::invalid_argument("Invalid hive approach speed");
    auto plan=movement::snapshot(source)->path("hive",{{"action",std::string("row")}},speed).at(0);
    return {std::move(plan),game::hive_proximity_prompt()};
}
namespace {
bool claimed_here(const std::shared_ptr<HiveSetupResult>& result,const ObservationContext& context,const Observation<bool>& proximity) {
    const auto claim=result?result->read():std::nullopt;
    return claim && claim->claimed_slot && Observation<bool>{Knowledge::present,true,claim->context.now,
        claim->context.window,claim->context.generation}.has_value(context,Time(2000))
        && !(proximity.usable(context,Time(2000),claim->context.now) && !proximity.value);
}
class AtHive final : public RunGuard {
    std::shared_ptr<const game::LandmarkSource> source_;
    bool ready_{},allow_claim_{};
    std::shared_ptr<HiveSetupResult> claimed_;
public:
    explicit AtHive(std::shared_ptr<const game::LandmarkSource> source,bool allow_claim=false,std::shared_ptr<HiveSetupResult> claimed={})
        :source_(std::move(source)),allow_claim_(allow_claim),claimed_(std::move(claimed)) {}
    std::vector<Rect> text_regions() const override { return !ready_ && allow_claim_?std::vector<Rect>{game::hive_claim_prompt().region}:std::vector<Rect>{}; }
    GuardDecision evaluate(const View& view,Time) override {
        if (!ready_) {
            const auto reading=source_->read(view.context);
            ready_=claimed_here(claimed_,view.context,reading) || (reading.has_value(view.context,Time(2000)) && reading.value);
            if (!ready_ && allow_claim_ && view.observations) {
                const auto claim=game::read_prompt_landmark(view.context,view.observations->text,game::hive_claim_prompt());
                ready_=claim.has_value(view.context,Time(2000)) && claim.value;
            }
        }
        return {ready_?GuardState::ready:GuardState::waiting,ready_?"Hive proximity confirmed":"Waiting for your hive interaction prompt"};
    }
};
class PrepareHiveView final : public Program {
    enum class Stage { claim, unlock, zoom, pitch_limit, pitch, observe, turn, lower, raise, reference, restore, zoom_out, done };
    std::shared_ptr<const game::LandmarkSource> reference_,proximity_;
    std::shared_ptr<HiveSetupResult> result_;
    Stage stage_{Stage::claim};
    Time minimum_{},seen_{Time(-1)};
    Observation<bool> heading_;
    unsigned turns_{};
    bool lowered_{};
    bool started_{};
    void observe(Time finished) {
        stage_=Stage::observe; minimum_=finished+Time(150); seen_=Time(-1);
    }
public:
    PrepareHiveView(std::shared_ptr<const game::LandmarkSource> reference,std::shared_ptr<const game::LandmarkSource> proximity,
        std::shared_ptr<HiveSetupResult> result)
        :reference_(std::move(reference)),proximity_(std::move(proximity)),result_(std::move(result)) {
        if (!reference_ || !proximity_) throw std::invalid_argument("Hive preparation requires proximity and visual observations");
    }
    RunConfig config() const override { return {Time(60000),28,{},true,true}; }
    std::unique_ptr<RunGuard> guard() const override { return std::make_unique<AtHive>(proximity_,true,result_); }
    Step next(const View& view) override {
        if (!started_) {
            if (result_) result_->begin_preparation(view.context);
            started_=true;
        }
        switch (stage_) {
        case Stage::claim: {
            const auto proximity=proximity_->read(view.context);
            if (claimed_here(result_,view.context,proximity) || (proximity.has_value(view.context,Time(2000)) && proximity.value)) {
                reference_->read(view.context);
                stage_=Stage::unlock; return next(view);
            }
            if (result_) result_->reset();
            return UsePrompt{game::hive_claim_prompt(),game::hive_proximity_prompt(),Time(15000),true};
        }
        case Stage::unlock: return RunProgram{[] { return set_shift_lock(false); }};
        case Stage::zoom: return MoveCamera{{Key::zoom_in,10}};
        case Stage::pitch_limit: return MoveCamera{game::pitch_limit(Key::pitch_up)};
        case Stage::pitch: return MoveCamera{{Key::pitch_down,4}};
        case Stage::turn: return MoveCamera{{Key::rotate_right,turns_?1u:4u,Time(100),Time(50)}};
        case Stage::lower: return MoveCamera{{Key::pitch_down,1}};
        case Stage::raise: return MoveCamera{{Key::pitch_up,1}};
        case Stage::reference: return AdoptHeading{0,heading_};
        case Stage::restore: return MoveCamera{{Key::pitch_up,1}};
        case Stage::zoom_out: return MoveCamera{{Key::zoom_out,5}};
        case Stage::done:
            if (result_) result_->prepared(view.context);
            return Done{"Hive camera prepared; departure position is not yet established"};
        case Stage::observe: break;
        }
        const auto& c=view.context;
        const auto value=reference_->read(c);
        if (value.usable(c,Time(2000),minimum_) && value.captured>seen_) {
            seen_=value.captured;
            if (value.knowledge==Knowledge::present && value.value) {
                heading_=value;
                stage_=Stage::reference;
                return next(view);
            }
            // Try the opposite spawn direction first. If it misses, inspect
            // both pitches at each heading during one full correction sweep.
            // Only a fresh honeycomb match establishes the heading.
            if (!turns_) stage_=Stage::turn;
            else if (!lowered_) stage_=Stage::lower;
            else if (turns_<=8) stage_=Stage::raise;
            else throw HiveAlignmentError("Hive reference was not found at either camera pitch");
            return next(view);
        }
        if (c.now-minimum_>=Time(2000))
            throw HiveAlignmentError("No fresh hive camera frame; alignment was not confirmed");
        return Wait{Time(50),"Checking the settled hive view"};
    }
    void step_completed(const View& view) override {
        switch (stage_) {
        case Stage::claim:
            reference_->read(view.context); stage_=Stage::unlock; break;
        case Stage::unlock: stage_=Stage::zoom; break;
        case Stage::zoom: stage_=Stage::pitch_limit; break;
        case Stage::pitch_limit: stage_=Stage::pitch; break;
        case Stage::pitch: observe(view.context.now); break;
        case Stage::turn: ++turns_; observe(view.context.now); break;
        case Stage::lower: lowered_=true; observe(view.context.now); break;
        case Stage::raise: lowered_=false; stage_=Stage::turn; break;
        case Stage::reference: stage_=lowered_?Stage::restore:Stage::zoom_out; break;
        case Stage::restore: stage_=Stage::zoom_out; break;
        case Stage::zoom_out: stage_=Stage::done; break;
        default: break;
        }
    }
    void finished(State state) override { if (result_ && state!=State::completed) result_->reset(); }
};
}
std::unique_ptr<Program> prepare_hive_view(std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,std::shared_ptr<HiveSetupResult> result) {
    return std::make_unique<PrepareHiveView>(std::move(reference),std::move(proximity),std::move(result));
}
}
