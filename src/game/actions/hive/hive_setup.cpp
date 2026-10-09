#include "hive_setup.hpp"
#include "game/actions/field/shift_lock.hpp"

namespace hive::automation {
std::optional<HiveSetupSnapshot> HiveSetupResult::read() const { std::lock_guard lock(mutex_); return value_; }
void HiveSetupResult::reset() { std::lock_guard lock(mutex_); value_.reset(); }
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
ApproachLandmark hive_reference_search(std::shared_ptr<const game::LandmarkSource> reference,movement::Source movement) {
    if (!reference) throw std::invalid_argument("Hive search requires observed proximity and visual reference");
    auto plan=movement::snapshot(movement)->path("hive-search",{},28).at(0);
    return {std::move(plan),std::move(reference)};
}
namespace {
class AtHive final : public RunGuard {
    std::shared_ptr<const game::LandmarkSource> source_;
    bool ready_{},allow_claim_{};
public:
    explicit AtHive(std::shared_ptr<const game::LandmarkSource> source,bool allow_claim=false)
        :source_(std::move(source)),allow_claim_(allow_claim) {}
    bool needs_ocr() const noexcept override { return true; }
    GuardDecision evaluate(const View& view,Time started) override {
        if (!ready_) {
            const auto reading=source_->read(view.context);
            ready_=reading.has_value(view.context,Time(2000),started) && reading.value;
            if (!ready_ && allow_claim_ && view.observations) {
                const auto claim=game::read_prompt_landmark(view.context,view.observations->text,game::hive_claim_prompt());
                ready_=claim.has_value(view.context,Time(2000),started) && claim.value;
            }
        }
        return {ready_?GuardState::ready:GuardState::waiting,ready_?"Hive proximity confirmed":"Waiting for your hive interaction prompt"};
    }
};
class FindHiveReference final : public Program {
    ApproachLandmark search_;
    std::shared_ptr<const game::LandmarkSource> proximity_;
public:
    FindHiveReference(std::shared_ptr<const game::LandmarkSource> source,std::shared_ptr<const game::LandmarkSource> proximity,movement::Source movement)
        :search_(hive_reference_search(std::move(source),std::move(movement))),proximity_(std::move(proximity)) {
        if (!proximity_) throw std::invalid_argument("Hive search requires a proximity source");
    }
    RunConfig config() const override { return {Time(50000),28,{},true,true}; }
    std::unique_ptr<RunGuard> guard() const override { return std::make_unique<AtHive>(proximity_); }
    Step next(const View& view) override {
        if (!view.completed_steps) { search_.source->read(view.context); return search_; }
        return Done{"Hive visual reference found"};
    }
};
class PrepareHiveView final : public Program {
    enum class Stage { claim, confirm_claim, unlock, zoom, pitch_limit, pitch, observe, turn, alternate, reference, restore, zoom_out, done };
    std::shared_ptr<const game::LandmarkSource> reference_,proximity_;
    std::shared_ptr<HiveSetupResult> result_;
    Stage stage_{Stage::claim};
    Time minimum_{},seen_{Time(-1)};
    Observation<bool> heading_;
    unsigned turns_{};
    bool alternate_{};
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
    std::unique_ptr<RunGuard> guard() const override { return std::make_unique<AtHive>(proximity_,true); }
    Step next(const View& view) override {
        if (!started_) {
            if (result_) result_->begin_preparation(view.context);
            started_=true;
        }
        switch (stage_) {
        case Stage::claim: {
            const auto proximity=proximity_->read(view.context);
            if (proximity.has_value(view.context,Time(2000)) && proximity.value) {
                reference_->read(view.context);
                stage_=Stage::unlock; return next(view);
            }
            if (result_) result_->reset();
            return UsePrompt{game::hive_claim_prompt(),game::hive_proximity_prompt()};
        }
        case Stage::unlock: return RunProgram{[] { return set_shift_lock(false); }};
        case Stage::zoom: return MoveCamera{{Key::zoom_in,10}};
        case Stage::pitch_limit: return MoveCamera{{Key::pitch_up,10}};
        case Stage::pitch: return MoveCamera{{Key::pitch_down,4}};
        case Stage::turn: return MoveCamera{{Key::rotate_right,1}};
        case Stage::alternate: return MoveCamera{{Key::pitch_down,1}};
        case Stage::reference: return AdoptHeading{0,heading_};
        case Stage::restore: return MoveCamera{{Key::pitch_up,1}};
        case Stage::zoom_out: return MoveCamera{{Key::zoom_out,5}};
        case Stage::done:
            if (result_) result_->prepared(view.context);
            return Done{"Hive camera prepared; departure position is not yet established"};
        case Stage::confirm_claim:
        case Stage::observe: break;
        }
        const auto& c=view.context;
        const bool confirming_claim=stage_==Stage::confirm_claim;
        if (confirming_claim && c.now-minimum_>=Time(5000)) throw HiveAlignmentError("Claimed hive proximity was not confirmed");
        const auto value=(confirming_claim?proximity_:reference_)->read(c);
        if (value.usable(c,Time(2000),minimum_) && value.captured>seen_) {
            seen_=value.captured;
            if (value.knowledge==Knowledge::present && value.value) {
                if (confirming_claim) reference_->read(c);
                heading_=value;
                stage_=confirming_claim?Stage::unlock:Stage::reference;
                return next(view);
            }
            if (confirming_claim) throw HiveAlignmentError("Hive proximity was lost during preparation");
            if (turns_<8) stage_=Stage::turn;
            else if (!alternate_) stage_=Stage::alternate;
            else throw HiveAlignmentError("Hive reference was not found at either camera pitch");
            return next(view);
        }
        return Wait{Time(50),confirming_claim?"Confirming claimed hive proximity":"Checking the settled hive view"};
    }
    void step_completed(const View& view) override {
        switch (stage_) {
        case Stage::claim:
            stage_=Stage::confirm_claim; minimum_=view.context.now+Time(1); seen_=Time(-1); break;
        case Stage::unlock: stage_=Stage::zoom; break;
        case Stage::zoom: stage_=Stage::pitch_limit; break;
        case Stage::pitch_limit: stage_=Stage::pitch; break;
        case Stage::pitch: observe(view.context.now); break;
        case Stage::turn: ++turns_; observe(view.context.now); break;
        case Stage::alternate: alternate_=true; turns_=0; observe(view.context.now); break;
        case Stage::reference: stage_=alternate_?Stage::restore:Stage::zoom_out; break;
        case Stage::restore: stage_=Stage::zoom_out; break;
        case Stage::zoom_out: stage_=Stage::done; break;
        default: break;
        }
    }
    void finished(State state) override { if (result_ && state!=State::completed) result_->reset(); }
};
}
std::unique_ptr<Program> find_hive_reference(std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,movement::Source movement) {
    return std::make_unique<FindHiveReference>(std::move(reference),std::move(proximity),std::move(movement));
}
std::unique_ptr<Program> prepare_hive_view(std::shared_ptr<const game::LandmarkSource> reference,
    std::shared_ptr<const game::LandmarkSource> proximity,std::shared_ptr<HiveSetupResult> result) {
    return std::make_unique<PrepareHiveView>(std::move(reference),std::move(proximity),std::move(result));
}
}
