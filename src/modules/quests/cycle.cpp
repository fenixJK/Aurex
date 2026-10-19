#include "cycle.hpp"
#include "game/actions/interaction/conversation.hpp"
#include "game/actions/field/shift_lock.hpp"
#include "game/data/npcs.hpp"
#include <algorithm>

namespace hive::features::quests {
namespace {
bool complete(const Card& card) {
    return card.complete_view && !card.objectives.empty() && std::all_of(card.objectives.begin(),card.objectives.end(),[](const auto& row) {
        return row.known && row.kind!=Kind::unsupported && row.required && row.collected==row.required;
    });
}
class Cycle final : public automation::Program {
    enum class Stage { scan,choose,prepare,travel,talk,verify,returning,returned,assess,action_start,action_verify,action_assess,done } stage_{Stage::scan};
    CyclePlan plan_;
    std::shared_ptr<const BrowserSource> browser_;
    std::shared_ptr<ScanResults> scan_;
    std::shared_ptr<CycleResults> results_;
    std::string giver_;
    GiverStatus status_;
    bool submitting_{},unresolved_before_{};
    std::string deferred_reason_;
    unsigned conversations_{},recoveries_{};
    std::set<std::string> attempted_actions_;
    automation::ProgramFactory action_;
    std::string retryable_action_;
    Time scan_since_{},accepted_{};
    std::optional<Observation<Card>> return_card_;
    std::optional<Card> card(const automation::View& view) const {
        const auto snapshot=scan_->snapshot();
        if (!snapshot || snapshot->context.generation!=view.context.generation || (snapshot->context.window.id!=view.context.window.id || snapshot->context.window.process!=view.context.window.process || snapshot->context.window.bounds!=view.context.window.bounds)
            || snapshot->context.now<scan_since_ || snapshot->context.now>view.context.now || view.context.now-snapshot->context.now>Time(60000))
            throw std::runtime_error("Quest cycle needs a completed scan in this session");
        std::optional<Card> found;
        for (const auto& observed:snapshot->cards) if (observed.value.giver==giver_) {
            if (!observed.has_value(view.context,Time(60000),scan_since_)) continue;
            if (found) throw std::runtime_error("Multiple quests for the selected giver are ambiguous");
            found=observed.value;
        }
        return found;
    }
    automation::Step scan(const automation::View& view,Stage next) {
        scan_since_=view.context.now; stage_=next;
        return automation::RunProgram{[source=browser_,results=scan_,givers=next==Stage::assess?std::vector<std::string>{giver_}:plan_.givers] { return browser_program(source,results,false,{givers.begin(),givers.end()}); }};
    }
    automation::Step finish_visit(const automation::View& view,std::string message,Time delay) {
        status_.status=std::move(message); status_.next_visit=delay>Time{}?accepted_+delay:view.context.now; results_->publish(giver_,status_);
        if (plan_.cooldowns && delay>Time{}) plan_.cooldowns->remember(game::npc(giver_).id,accepted_,status_.next_visit,false,view.context);
        stage_=Stage::done;
        return deferred_reason_.empty()?automation::Step(automation::Done{"Quest visit finished"}):automation::Step(automation::Defer{deferred_reason_});
    }
public:
    Cycle(CyclePlan plan,std::shared_ptr<const BrowserSource> browser,std::shared_ptr<ScanResults> scan,std::shared_ptr<CycleResults> results)
        :plan_(std::move(plan)),browser_(std::move(browser)),scan_(std::move(scan)),results_(std::move(results)) {
        if (!browser_ || !scan_ || !results_ || !plan_.prepare || !plan_.return_to_hive || !plan_.conversation
            || !std::isfinite(plan_.speed) || plan_.speed<8 || plan_.speed>200 || plan_.givers.size()>game::npcs().size())
            throw std::invalid_argument("Quest cycle needs travel, conversation and scan services");
        std::set<std::string> unique;
        for (const auto& name:plan_.givers) { game::npc(name); if (!unique.insert(name).second) throw std::invalid_argument("Duplicate quest giver"); }
    }
    automation::RunConfig config() const override { return {std::chrono::hours(1),plan_.speed,{},true,true}; }
    std::optional<game::MenuTab> movement_menu() const override {
        if (stage_==Stage::returned || (stage_==Stage::action_verify && !retryable_action_.empty())) return game::MenuTab::quests;
        return {};
    }
    ActivityStatus activity_status() const override {
        switch (stage_) {
        case Stage::prepare: return {};
        case Stage::travel: case Stage::talk: return {"Visiting",giver_};
        case Stage::returning: case Stage::returned: return {"Returning to hive",{}};
        case Stage::verify: return {submitting_ && conversations_==1?"Turning in quest":"Getting quest",giver_};
        case Stage::action_start: case Stage::action_verify: return {status_.status,giver_};
        default: return {};
        }
    }
    automation::ProgramFactory recovery(automation::Interruption reason,const automation::View& view) override {
        if ((reason!=automation::Interruption::death && reason!=automation::Interruption::lost_route && reason!=automation::Interruption::lost_observations)
            || !plan_.return_to_hive || recoveries_>=2) return {};
        ++recoveries_;
        if (!giver_.empty()) {
            status_.status="Visit interrupted; checking quests again after recovery"; results_->publish(giver_,status_);
        }
        resume_after_reconnect(view);
        if (reason==automation::Interruption::lost_observations) {
            deferred_reason_="Quest log unreadable; retry after other work";
            stage_=Stage::done;
        }
        return plan_.return_to_hive;
    }
    bool resume_after_reconnect(const automation::View& view) override {
        if (!retryable_action_.empty()) attempted_actions_.erase(retryable_action_);
        retryable_action_.clear();
        stage_=deferred_reason_.empty()?Stage::scan:Stage::done;
        scan_since_=view.context.now; conversations_=0; return_card_.reset(); action_={};
        return true;
    }
    void observe(const automation::View& view) override {
        if (stage_!=Stage::returned || return_card_) return;
        try {
            const auto scene=browser_->read();
            if (!scene.has_value(view.context,Time(2000),scan_since_)) return;
            for (const auto& current:scene.value.cards)
                if (current.value.giver==giver_ && current.value.complete_view
                    && current.has_value(view.context,Time(2000),scan_since_)
                    && std::any_of(current.value.objectives.begin(),current.value.objectives.end(),[](const auto& row) {
                        return row.known && row.required && row.collected<row.required;
                    })) {
                    return_card_=current; break;
                }
        } catch (const std::exception&) {
            // A quest read must not interrupt returning home; scan after arrival if needed.
        }
    }
    automation::Step next(const automation::View& view) override {
        using namespace automation;
        if (plan_.givers.empty()) return Done{"No quest givers selected"};
        if (stage_==Stage::scan) {
            if (plan_.cooldowns) { plan_.cooldowns->load(); plan_.cooldowns->flush(); }
            return scan(view,Stage::choose);
        }
        if (stage_==Stage::choose) {
            const auto remembered=results_->read();
            for (const auto& giver:plan_.givers) {
                giver_=giver; status_=remembered.contains(giver)?remembered.at(giver):GiverStatus{};
                const auto current=card(view);
                if (current && !complete(*current)) continue;
                const auto& npc=game::npc(giver);
                const auto remaining=plan_.cooldowns?plan_.cooldowns->remaining(npc.id,npc.quest_interval):Time{};
                if (remaining>Time{}) {
                    status_.next_visit=view.context.now+remaining; status_.status="Next quest on cooldown";
                    results_->publish(giver,status_);
                }
                if (view.context.now<status_.next_visit) continue;
                if (!current && !scan_->snapshot()->reached_end) continue;
                unresolved_before_=!current && !scan_->snapshot()->names_complete;
                submitting_=current.has_value();
                status_.status=unresolved_before_?"Checking quest with giver":submitting_?"Submitting quest":"Receiving quest"; results_->publish(giver_,status_);
                stage_=Stage::prepare; return RunProgram{[] { return set_shift_lock(false); }};
            }
            for (const auto& giver:plan_.givers) {
                giver_=giver; status_=remembered.contains(giver)?remembered.at(giver):GiverStatus{};
                const auto current=card(view); if (!current) continue;
                for (const auto& objective:current->objectives) {
                    if (!objective.known || objective.collected>=objective.required || attempted_actions_.contains(giver+"/"+objective.target)) continue;
                    const auto provider=plan_.actions.find(objective.target);
                    if (provider==plan_.actions.end() || !provider->second.program || (provider->second.available && !provider->second.available())) continue;
                    attempted_actions_.insert(giver+"/"+objective.target);
                    retryable_action_=objective.kind==Kind::petals?giver+"/"+objective.target:std::string{};
                    status_.status="Working on "+objective.target; results_->publish(giver_,status_);
                    action_=[create=provider->second.program,objective] { return create(objective); };
                    stage_=Stage::action_start;
                    if (provider->second.prerequisite) return RunProgram{provider->second.prerequisite};
                    return next(view);
                }
            }
            return Done{"Quest objectives refreshed"};
        }
        if (stage_==Stage::prepare) { stage_=Stage::travel; return RunProgram{plan_.prepare}; }
        if (stage_==Stage::travel) { stage_=Stage::talk; return Navigate{game::npc(giver_).location()}; }
        if (stage_==Stage::talk) {
            ++conversations_; stage_=Stage::verify;
            return RunProgram{[source=plan_.conversation] { return converse(source); }};
        }
        if (stage_==Stage::action_start) { stage_=Stage::action_verify; return RunProgram{std::move(action_)}; }
        if (stage_==Stage::action_verify) { retryable_action_.clear(); return scan(view,Stage::action_assess); }
        if (stage_==Stage::action_assess) {
            status_.status="Action finished; quest progress rechecked"; results_->publish(giver_,status_);
            stage_=Stage::choose; return next(view);
        }
        if (stage_==Stage::verify) {
            if (conversations_<((submitting_ || unresolved_before_)?2u:1u)) { stage_=Stage::talk; return next(view); }
            scan_since_=accepted_=view.context.now; return_card_.reset(); stage_=Stage::returning;
            if (view.observations && view.observations->menus.has_value(view.context,Time(2000)))
                for (const auto& menu:view.observations->menus.value)
                    if (menu.tab==game::MenuTab::quests && menu.selected==false)
                        return UseControl{{Knowledge::present,menu.center(),view.observations->menus.captured,view.context.window,view.context.generation},0,"Opening quest log"};
        }
        if (stage_==Stage::returning) { stage_=Stage::returned; return RunProgram{plan_.return_to_hive}; }
        if (stage_==Stage::returned) {
            if (!return_card_ || !return_card_->has_value(view.context,Time(60000),scan_since_)) return scan(view,Stage::assess);
            scan_->complete({view.context,{*return_card_},false});
            unsigned readable=0; for (const auto& row:return_card_->value.objectives) readable+=row.known;
            scan_->publish({"Quest read during return",1,1,readable,unsigned(return_card_->value.objectives.size())-readable,true});
            stage_=Stage::assess;
        }
        if (stage_==Stage::assess) {
            const auto current=card(view);
            const bool restarted=current && current->complete_view && std::any_of(current->objectives.begin(),current->objectives.end(),[](const auto& row) {
                return row.known && row.required && row.collected<row.required;
            });
            if (restarted) {
                if (unresolved_before_) { status_.quest=current->quest; return finish_visit(view,"Quest identified; earlier state was unreadable",Time{}); }
                if (submitting_) ++status_.completed;
                ++status_.received; status_.quest=current->quest;
                return finish_visit(view,"Quest received",game::npc(giver_).quest_interval);
            }
            deferred_reason_="Quest change not confirmed; retry after other work";
            return finish_visit(view,"Quest change not confirmed",Time{});
        }
        if (!deferred_reason_.empty()) return Defer{deferred_reason_};
        return Done{"Quest visit finished"};
    }
    void finished(automation::State state) override {
        if (state!=automation::State::completed && !giver_.empty()) {
            status_.status="Visit interrupted; quest outcome not assumed"; results_->publish(giver_,status_);
        }
    }
};
}
std::unique_ptr<automation::Program> cycle_program(CyclePlan plan,std::shared_ptr<const BrowserSource> browser,
    std::shared_ptr<ScanResults> scan,std::shared_ptr<CycleResults> results) {
    return std::make_unique<Cycle>(std::move(plan),std::move(browser),std::move(scan),std::move(results));
}
}
