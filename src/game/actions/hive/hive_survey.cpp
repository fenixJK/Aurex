#include "hive_setup.hpp"
#include <algorithm>

namespace hive::automation {
namespace {
ApproachPrompt claim_approach(unsigned slot,double speed,const movement::Recipes& recipes) {
    auto plan=recipes.path("hive",{{"action",std::string("claim")},{"slot",int(slot)}},speed).at(0);
    return {std::move(plan),game::hive_claim_prompt()};
}
class HiveSurvey final : public Program {
    enum class Stage { zoom,pitch,observe,turn,restore,departure_pitch,departure_zoom,approach,check,scan,claim,done };
    Stage stage_{Stage::zoom};
    std::shared_ptr<const game::HiveSlotsSource> source_;
    Time minimum_{},seen_{Time(-1)};
    bool turned_{};
    std::string result_;
    std::optional<HiveClaimOptions> claim_;
    std::shared_ptr<const movement::Recipes> movement_;
    unsigned selected_{},next_slot_{};
    std::array<bool,6> checked_{};
    void start_scan() {
        selected_=claim_->preferred_slot?claim_->preferred_slot:1;
        stage_=Stage::departure_pitch;
    }
    bool can_scan() const { return claim_ && claim_->confirmed_spawn && !claim_->search_opposite_view; }
    void check_site(Time now) {
        stage_=Stage::check; minimum_=now+Time(150); seen_=Time(-1);
    }
    Step check_slot(const View& view) {
        const auto& c=view.context;
        if (c.now-minimum_>=Time(4000)) throw ObservationUnavailable("Hive slot prompt could not be read");
        const auto wait=[&] { return Step(Wait{Time(50),"Checking hive slot "+std::to_string(selected_)}); };
        if (!view.observations) return wait();
        const auto& text=view.observations->text;
        if (!text.has_value(c,Time(2000),minimum_) || text.captured<=seen_) return wait();
        seen_=text.captured;
        const auto owned=game::read_prompt_landmark(c,text,game::hive_proximity_prompt());
        const auto available=game::read_prompt_landmark(c,text,game::hive_claim_prompt());
        if ((owned.value && available.value) || (!owned.value && !available.value
            && (!owned.usable(c,Time(2000),minimum_) || !available.usable(c,Time(2000),minimum_)))) {
            return wait();
        }
        const int candidate=owned.value?1:available.value?2:0;
        if (candidate==1) {
            if (claim_->result) claim_->result->claimed(selected_,c);
            result_="Owned hive slot "+std::to_string(selected_)+" confirmed"; stage_=Stage::done; return next(view);
        }
        if (candidate==2) { stage_=Stage::claim; return next(view); }
        checked_[selected_-1]=true; next_slot_=0;
        for (unsigned slot=1;slot<=6;++slot) if (!checked_[slot-1]) {
            const auto distance=std::abs(int(slot)-int(selected_));
            const auto previous=std::abs(int(next_slot_)-int(selected_));
            if (!next_slot_ || distance<previous || (distance==previous && selected_>3)) next_slot_=slot;
        }
        if (!next_slot_) throw ClientRejoinRequired("No owned or claimable hive found; rejoining Bee Swarm");
        stage_=Stage::scan; return next(view);
    }
    bool started_{};
public:
    explicit HiveSurvey(std::shared_ptr<const game::HiveSlotsSource> source,std::optional<HiveClaimOptions> claim={})
        :source_(std::move(source)),claim_(claim) {
        if (can_scan()) stage_=Stage::pitch;
        if (!source_) throw std::invalid_argument("Hive survey requires slot observations");
        if (claim_ && claim_->confirmed_spawn && claim_->search_opposite_view)
            throw std::invalid_argument("Confirmed spawn scans must preserve their camera orientation");
        if (claim_ && (!std::isfinite(claim_->speed) || claim_->speed<8 || claim_->speed>200 || claim_->preferred_slot>6))
            throw std::invalid_argument("Hive claim requires a speed from 8 to 200 and preferred slot from 0 to 6");
    }
    RunConfig config() const override { return {claim_?(can_scan()?Time(120000):Time(60000)):Time(15000),claim_?claim_->speed:28,{},bool(claim_),true}; }
    std::vector<Rect> text_regions() const override { return stage_==Stage::check?std::vector<Rect>{game::hive_claim_prompt().region}:std::vector<Rect>{}; }
    Step next(const View& view) override {
        if (!started_) { if (claim_ && claim_->result) claim_->result->reset(); started_=true; }
        switch (stage_) {
        case Stage::zoom: return MoveCamera{{Key::zoom_in,10}};
        case Stage::pitch: return MoveCamera{{Key::pitch_up,can_scan()?5u:10u}};
        case Stage::turn:
        case Stage::restore: return MoveCamera{{Key::rotate_right,4}};
        case Stage::departure_pitch: return MoveCamera{{Key::pitch_down,4}};
        case Stage::departure_zoom: return MoveCamera{{Key::zoom_out,5}};
        case Stage::approach: {
            if (!movement_) movement_=movement::snapshot(claim_->movement);
            auto approach=claim_approach(selected_,claim_->speed,*movement_);
            if (!can_scan()) return approach;
            approach.movement.detector.clear();
            approach.movement.completion=workflows::CheckpointCompletion::movement;
            return FollowPath{std::move(approach.movement)};
        }
        case Stage::check: return check_slot(view);
        case Stage::scan: return FollowPath{movement_->path("hive",{{"action",std::string("scan")},
            {"from",int(selected_)},{"to",int(next_slot_)}},claim_->speed).at(0)};
        case Stage::claim: return UsePrompt{game::hive_claim_prompt(),game::hive_proximity_prompt(),Time(15000),true};
        case Stage::done: return Done{result_};
        case Stage::observe: break;
        }
        const auto& c=view.context;
        if (c.now-minimum_>=Time(5000)) {
            if (can_scan()) { start_scan(); return next(view); }
            throw std::runtime_error("Hive survey could not confirm a stable view");
        }
        const auto reading=source_->read(c);
        if (reading.has_value(c,Time(2000),minimum_) && reading.captured>seen_) {
            seen_=reading.captured;
            std::string slots;
            for (std::size_t i=0;i<reading.value.size();++i) if (reading.value[i]==Knowledge::present) {
                if (!slots.empty()) slots+=", ";
                slots+=std::to_string(i+1);
            }
            if (!slots.empty()) {
                result_="Candidate hive slots: "+slots+". Confirm at the hive before claiming.";
                if (claim_) {
                    selected_=claim_->preferred_slot;
                    if (!selected_ || reading.value[selected_-1]!=Knowledge::present)
                        selected_=static_cast<unsigned>(std::find(reading.value.begin(),reading.value.end(),Knowledge::present)-reading.value.begin()+1);
                    stage_=Stage::departure_pitch;
                } else stage_=Stage::done;
            } else if (std::all_of(reading.value.begin(),reading.value.end(),[](auto value) { return value==Knowledge::absent; })) {
                if (can_scan()) { start_scan(); return next(view); }
                if (claim_ && !claim_->search_opposite_view) throw std::runtime_error("No empty hive candidates in the confirmed spawn view");
                if (!turned_) stage_=Stage::turn;
                else {
                    result_="No empty-hive candidates found. The hives may be occupied or the spawn view unsupported.";
                    stage_=Stage::restore;
                }
            }
            if (stage_!=Stage::observe) return next(view);
        }
        return Wait{Time(50),"Surveying hive slots from spawn"};
    }
    void step_completed(const View& view) override {
        if (stage_==Stage::zoom) stage_=Stage::pitch;
        else if (stage_==Stage::restore) {
            if (claim_) throw std::runtime_error("No empty hive was found; no claim was attempted");
            stage_=Stage::done;
        }
        else if (stage_==Stage::departure_pitch) stage_=Stage::departure_zoom;
        else if (stage_==Stage::departure_zoom) stage_=Stage::approach;
        else if (stage_==Stage::approach) {
            if (can_scan()) check_site(view.context.now); else stage_=Stage::claim;
        }
        else if (stage_==Stage::scan) { selected_=next_slot_; check_site(view.context.now); }
        else if (stage_==Stage::claim) {
            if (claim_->result) claim_->result->claimed(selected_,view.context);
            result_="Hive claim confirmed"; stage_=Stage::done;
        }
        else if (stage_==Stage::pitch || stage_==Stage::turn) {
            turned_=stage_==Stage::turn;
            stage_=Stage::observe; minimum_=view.context.now+Time(150); seen_=Time(-1);
        }
    }
    void finished(State state) override { if (claim_ && claim_->result && state!=State::completed) claim_->result->reset(); }
};
}
std::unique_ptr<Program> claim_hive_from_spawn(std::shared_ptr<const game::HiveSlotsSource> source,HiveClaimOptions options) {
    return std::make_unique<HiveSurvey>(std::move(source),options);
}
std::unique_ptr<Program> survey_hive_slots(std::shared_ptr<const game::HiveSlotsSource> source) {
    return std::make_unique<HiveSurvey>(std::move(source));
}
}
