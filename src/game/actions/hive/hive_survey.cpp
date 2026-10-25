#include "hive_setup.hpp"
#include <algorithm>

namespace hive::automation {
namespace {
workflows::CheckpointPlan walk_only(workflows::CheckpointPlan plan) {
    plan.detector.clear(); plan.completion=workflows::CheckpointCompletion::movement;
    plan.attempts=0; plan.nudge.clear(); plan.nudge_path.clear();
    return plan;
}
game::WalkLeg reverse_leg(game::WalkLeg leg) {
    auto reverse=[](Key key) { return key==Key::forward?Key::backward:key==Key::backward?Key::forward:
        key==Key::left?Key::right:Key::left; };
    leg.first=reverse(leg.first);
    if (leg.second) leg.second=reverse(*leg.second);
    return leg;
}
ApproachPrompt claim_approach(unsigned slot,double speed,const movement::Recipes& recipes) {
    auto plan=recipes.path("hive",{{"action",std::string("claim")},{"slot",int(slot)}},speed).at(0);
    return {std::move(plan),game::hive_claim_prompt()};
}
class HiveSurvey final : public Program {
    enum class Stage { zoom,pitch,observe,turn,restore,departure_pitch,departure_zoom,approach,check,correct,restore_position,scan,claim,done };
    Stage stage_{Stage::zoom};
    std::shared_ptr<const game::HiveSlotsSource> source_;
    Time minimum_{},seen_{Time(-1)};
    bool turned_{};
    std::string result_;
    HiveClaimOptions claim_;
    std::shared_ptr<const movement::Recipes> movement_;
    unsigned selected_{},next_slot_{},claim_attempts_{};
    Time pressed_{Time(-1)};
    std::array<bool,6> checked_{};
    workflows::CheckpointPlan approach_;
    unsigned corrections_{};
    void start_scan() {
        selected_=claim_.preferred_slot?claim_.preferred_slot:1;
        stage_=Stage::departure_pitch;
    }
    bool can_scan() const { return claim_.confirmed_spawn && !claim_.search_opposite_view; }
    void check_site(Time now) {
        stage_=Stage::check; minimum_=now+Time(150); seen_=Time(-1);
    }
    Step advance_slot(const View& view) {
        checked_[selected_-1]=true; next_slot_=0;
        for (unsigned slot=1;slot<=6;++slot) if (!checked_[slot-1]) {
            const auto distance=std::abs(int(slot)-int(selected_));
            const auto previous=std::abs(int(next_slot_)-int(selected_));
            if (!next_slot_ || distance<previous || (distance==previous && selected_>3)) next_slot_=slot;
        }
        if (!next_slot_) throw ObservationUnavailable("Hive row scan did not confirm an owned or claimable hive");
        stage_=corrections_?Stage::restore_position:Stage::scan; return next(view);
    }
    Step check_slot(const View& view) {
        const auto& c=view.context;
        const bool expired=c.now-minimum_>=Time(4000);
        const auto wait=[&] {
            if (expired) throw ObservationUnavailable("Hive slot prompt could not be read");
            return Step(Wait{Time(50),"Checking hive slot "+std::to_string(selected_)});
        };
        if (!view.observations) return wait();
        const auto& text=view.observations->text;
        if (!text.has_value(c,Time(2000),minimum_) || (!expired && text.captured<=seen_)) return wait();
        seen_=text.captured;
        const auto owned=game::read_prompt_landmark(c,text,game::hive_proximity_prompt());
        const auto available=game::read_prompt_landmark(c,text,game::hive_claim_prompt());
        if (owned.value && available.value) return wait();
        const int candidate=owned.value?1:available.value?2:0;
        if (candidate==1) {
            if (claim_.result) claim_.result->claimed(selected_,c);
            result_="Owned hive slot "+std::to_string(selected_)+" confirmed"; stage_=Stage::done; return next(view);
        }
        if (pressed_>=Time{} && text.captured<pressed_+Time(750)) return wait();
        if (candidate==2) {
            if (claim_attempts_>=3) throw ObservationUnavailable("Hive claim did not respond");
            stage_=Stage::claim; return next(view);
        }
        const auto& prompt=view.observations->interaction_prompt;
        if (!prompt.has_value(c,Time(2000),minimum_) || prompt.captured!=text.captured) return wait();
        const bool trade=game::read_prompt_landmark(c,text,{{"Trade Request"},
            game::interaction_prompt_search_region,game::hive_prompt_confidence,game::PromptMatch::phrase,true}).value;
        // After E, a missing caption can be the claim transition, not an occupied slot.
        if (claim_attempts_ && !trade) return wait();
        // One unrecognized banner does not invalidate the known row position.
        // Visit the remaining slots before resetting the acquisition attempt.
        const bool unreadable=!owned.usable(c,Time(2000),minimum_) || !available.usable(c,Time(2000),minimum_);
        if ((prompt.value || unreadable) && !trade) return expired?advance_slot(view):wait();
        if (!trade && corrections_<approach_.attempts && !approach_.nudge_path.empty()) {
            stage_=Stage::correct; return next(view);
        }
        return advance_slot(view);
    }
    bool started_{};
public:
    explicit HiveSurvey(std::shared_ptr<const game::HiveSlotsSource> source,HiveClaimOptions claim)
        :source_(std::move(source)),claim_(claim) {
        if (can_scan()) stage_=Stage::pitch;
        if (!source_) throw std::invalid_argument("Hive survey requires slot observations");
        if (claim_.confirmed_spawn && claim_.search_opposite_view)
            throw std::invalid_argument("Confirmed spawn scans must preserve their camera orientation");
        if (!std::isfinite(claim_.speed) || claim_.speed<8 || claim_.speed>200 || claim_.preferred_slot>6)
            throw std::invalid_argument("Hive claim requires a speed from 8 to 200 and preferred slot from 0 to 6");
    }
    RunConfig config() const override { return {can_scan()?Time(120000):Time(60000),claim_.speed,{},true,true}; }
    std::vector<Rect> text_regions() const override { return stage_==Stage::check || stage_==Stage::claim?std::vector<Rect>{game::hive_claim_prompt().region}:std::vector<Rect>{}; }
    Step next(const View& view) override {
        if (!started_) { if (claim_.result) claim_.result->reset(); started_=true; }
        switch (stage_) {
        case Stage::zoom: return MoveCamera{{Key::zoom_in,10}};
        case Stage::pitch: return MoveCamera{can_scan()?game::CameraMove{Key::pitch_up,5}:game::pitch_limit(Key::pitch_up)};
        case Stage::turn:
        case Stage::restore: return MoveCamera{{Key::rotate_right,4}};
        case Stage::departure_pitch: return MoveCamera{{Key::pitch_down,4}};
        case Stage::departure_zoom: return MoveCamera{{Key::zoom_out,5}};
        case Stage::approach: {
            if (!movement_) movement_=movement::snapshot(claim_.movement);
            auto approach=claim_approach(selected_,claim_.speed,*movement_);
            if (!can_scan()) return approach;
            approach_=std::move(approach.movement);
            return FollowPath{walk_only(approach_)};
        }
        case Stage::check: return check_slot(view);
        case Stage::correct:
        case Stage::restore_position: {
            auto plan=walk_only(approach_); plan.path=approach_.nudge_path;
            if (stage_==Stage::restore_position) {
                std::reverse(plan.path.begin(),plan.path.end());
                for (auto& leg:plan.path) { leg=reverse_leg(leg); leg.tiles*=corrections_; }
            }
            return FollowPath{std::move(plan)};
        }
        case Stage::scan: return FollowPath{movement_->path("hive",{{"action",std::string("scan")},
            {"from",int(selected_)},{"to",int(next_slot_)}},claim_.speed).at(0)};
        case Stage::claim: return Hold{{Key::interact},Time(100)};
        case Stage::done: return Done{result_};
        case Stage::observe: break;
        }
        const auto& c=view.context;
        if (c.now-minimum_>=Time(5000)) {
            throw std::runtime_error("Hive survey could not confirm a stable view");
        }
        const auto reading=source_->read(c);
        if (reading.has_value(c,Time(2000),minimum_) && reading.captured>seen_) {
            seen_=reading.captured;
            const auto candidate=std::find(reading.value.begin(),reading.value.end(),Knowledge::present);
            if (candidate!=reading.value.end()) {
                selected_=claim_.preferred_slot;
                if (!selected_ || reading.value[selected_-1]!=Knowledge::present)
                    selected_=static_cast<unsigned>(candidate-reading.value.begin()+1);
                stage_=Stage::departure_pitch;
            } else if (std::all_of(reading.value.begin(),reading.value.end(),[](auto value) { return value==Knowledge::absent; })) {
                if (!claim_.search_opposite_view) throw std::runtime_error("No empty hive candidates in the confirmed spawn view");
                if (!turned_) stage_=Stage::turn;
                else stage_=Stage::restore;
            }
            if (stage_!=Stage::observe) return next(view);
        }
        return Wait{Time(50),"Surveying hive slots from spawn"};
    }
    void step_completed(const View& view) override {
        if (stage_==Stage::zoom) stage_=Stage::pitch;
        else if (stage_==Stage::restore) throw std::runtime_error("No empty hive was found; no claim was attempted");
        else if (stage_==Stage::departure_pitch) stage_=Stage::departure_zoom;
        else if (stage_==Stage::departure_zoom) stage_=Stage::approach;
        else if (stage_==Stage::approach) {
            check_site(view.context.now);
        }
        else if (stage_==Stage::correct) {
            ++corrections_; check_site(view.context.now);
        }
        else if (stage_==Stage::restore_position) {
            corrections_=0; stage_=Stage::scan;
        }
        else if (stage_==Stage::scan) {
            selected_=next_slot_; claim_attempts_=0; pressed_=Time(-1); check_site(view.context.now);
        }
        else if (stage_==Stage::claim) {
            ++claim_attempts_; pressed_=view.context.now; check_site(view.context.now);
        }
        else if (stage_==Stage::pitch || stage_==Stage::turn) {
            if (can_scan()) { start_scan(); return; }
            turned_=stage_==Stage::turn;
            stage_=Stage::observe; minimum_=view.context.now+Time(150); seen_=Time(-1);
        }
    }
    void finished(State state) override { if (claim_.result && state!=State::completed) claim_.result->reset(); }
};
}
std::unique_ptr<Program> claim_hive_from_spawn(std::shared_ptr<const game::HiveSlotsSource> source,HiveClaimOptions options) {
    return std::make_unique<HiveSurvey>(std::move(source),options);
}
}
