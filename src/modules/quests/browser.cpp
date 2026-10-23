#include "browser.hpp"
#include "game/detection/menus.hpp"
#include <algorithm>
#include <charconv>
#include <string_view>
#include <tuple>

namespace hive::features::quests {
namespace {
bool same_page(std::string_view a,std::string_view b) {
    if (a==b) return true;
    while (!a.empty() && !b.empty()) {
        const auto end_a=a.find('\n'),end_b=b.find('\n');
        const auto line_a=a.substr(0,end_a),line_b=b.substr(0,end_b);
        const auto colon_a=line_a.find(':'),colon_b=line_b.find(':');
        if (colon_a==std::string_view::npos || colon_b==std::string_view::npos
            || line_a.substr(colon_a)!=line_b.substr(colon_b)) return false;
        int y_a{},y_b{};
        const auto parsed_a=std::from_chars(line_a.data(),line_a.data()+colon_a,y_a);
        const auto parsed_b=std::from_chars(line_b.data(),line_b.data()+colon_b,y_b);
        if (parsed_a.ec!=std::errc{} || parsed_b.ec!=std::errc{}
            || parsed_a.ptr!=line_a.data()+colon_a || parsed_b.ptr!=line_b.data()+colon_b
            || std::abs(y_a-y_b)>2) return false;
        a=end_a==std::string_view::npos?std::string_view{}:a.substr(end_a+1);
        b=end_b==std::string_view::npos?std::string_view{}:b.substr(end_b+1);
    }
    return a.empty() && b.empty();
}
class Browser final : public automation::Program {
    enum class Stage { initial,opening,up,down,closing,done };
    std::shared_ptr<const BrowserSource> source_;
    std::shared_ptr<ScanResults> results_;
    std::set<std::string> requested_;
    Stage stage_{Stage::initial};
    ScanReport report_;
    Time began_{Time(-1)},minimum_{},last_frame_{Time(-1)},deadline_{};
    unsigned scrolls_{},unchanged_{};
    unsigned open_attempts_{},close_attempts_{};
    std::string deferred_reason_;
    bool action_{},opened_{},close_when_finished_{};
    std::string before_;
    std::optional<Point> scroll_anchor_;
    std::map<std::string,Observation<Card>> confirmed_;
    bool reached_end_{},names_complete_{true};
    void publish_cards(const ObservationContext& context) {
        ScannedQuests snapshot{context,{},reached_end_,names_complete_};
        for (const auto& [id,card]:confirmed_) snapshot.cards.push_back(card);
        results_->publish(std::move(snapshot));
    }
    automation::Step wait(const std::string& reason) {
        report_.status=reason; results_->publish(report_); return automation::Wait{Time(100),reason};
    }
    automation::Step use(const Observation<BrowserScene>& scene,Point point,int scroll,const std::string& reason) {
        action_=true; report_.status=reason; results_->publish(report_);
        return automation::UseControl{{Knowledge::present,point,scene.captured,scene.window,scene.generation},scroll,reason};
    }
    void remember(const Observation<BrowserScene>& scene) {
        names_complete_=names_complete_ && scene.value.names_complete;
        for (const auto& observed : scene.value.cards) {
            if (!observed.has_value({scene.window,scene.generation,scene.captured},Time(2000)) || observed.captured!=scene.captured) continue;
            const auto& card=observed.value; const auto key=card.giver+"/"+card.quest;
            const auto previous=confirmed_.find(key);
            if (confirmed_.size()>=32 && previous==confirmed_.end()) continue;
            const auto quality=[](const Card& value) {
                return std::tuple{value.complete_view,std::count_if(value.objectives.begin(),value.objectives.end(),
                    [](const auto& row) { return row.known; }),value.objectives.size()};
            };
            if (previous==confirmed_.end() || quality(previous->second.value)<=quality(card))
                confirmed_[key]=observed;
        }
        report_.quests=static_cast<unsigned>(confirmed_.size()); report_.readable=report_.unresolved=0;
        for (const auto& [id,card] : confirmed_) for (const auto& row : card.value.objectives) {
            if (row.known) ++report_.readable; else ++report_.unresolved;
        }
        // Share the page while its observations are fresh. A long scroll to
        // another giver must not expire the first giver before it is consumed.
        publish_cards({scene.window,scene.generation,scene.captured});
    }
    automation::Step finish(const Observation<BrowserScene>& scene,const automation::View& view) {
        if (close_when_finished_ && opened_ && (scene.value.panel_visible || scene.value.scroll)) {
            if (!scene.value.toggle) throw automation::ObservationUnavailable("Quest scan could not locate the control to close the log");
            stage_=Stage::closing; ++close_attempts_;
            return use(scene,*scene.value.toggle,0,"Closing quest log");
        }
        stage_=Stage::done; report_.finished=true; report_.status="Quest scan finished; only observed rows are included";
        publish_cards({scene.window,scene.generation,scene.captured});
        results_->publish(report_);
        return automation::Hold{{},Time(1),game::pointer_rest(view.context,view.observations?*view.observations:game::State{})};
    }
    automation::Step defer(const Observation<BrowserScene>& scene,std::string reason) {
        if (scene.value.panel_visible || scene.value.scroll) {
            if (!scene.value.toggle) throw automation::ObservationUnavailable("Quest log cannot be closed from the current reading");
            deferred_reason_=std::move(reason); opened_=true; stage_=Stage::closing; ++close_attempts_;
            return use(scene,*scene.value.toggle,0,"Closing unreadable quest log");
        }
        report_.finished=false; report_.status=std::move(reason); results_->publish(report_);
        return automation::Defer{report_.status};
    }
public:
    Browser(std::shared_ptr<const BrowserSource> source,std::shared_ptr<ScanResults> results,bool close_when_finished,std::set<std::string> requested)
        :source_(std::move(source)),results_(std::move(results)),requested_(std::move(requested)),close_when_finished_(close_when_finished) {}
    automation::RunConfig config() const override { return {Time(60000),28,{},false,true}; }
    ActivityStatus activity_status() const override { return {"Reading quests",{}}; }
    automation::Step next(const automation::View& view) override {
        if (stage_==Stage::done) return automation::Done{report_.status};
        if (began_<Time::zero()) { began_=view.context.now; minimum_=began_; deadline_=began_+Time(5000); report_={}; results_->publish(report_); }
        const auto scene=source_->read();
        if (!scene.has_value(view.context,Time(2000),minimum_) || scene.captured<=last_frame_) {
            if (view.context.now>=deadline_ && !scene.has_value(view.context,Time(2000),minimum_)) {
                if (scene.knowledge==Knowledge::unknown) throw automation::ObservationUnavailable("Quest scan stopped: the detector has not returned a game frame");
                if (scene.generation!=view.context.generation || scene.window.id!=view.context.window.id
                    || scene.window.process!=view.context.window.process || scene.window.bounds!=view.context.window.bounds)
                    throw std::runtime_error("Quest scan stopped: the captured window no longer matches the session");
                throw automation::ObservationUnavailable("Quest scan stopped: waiting for a fresh menu reading");
            }
            return wait("Waiting for quest log");
        }
        if (last_frame_<Time{}) deadline_=view.context.now+Time(5000);
        last_frame_=scene.captured;
        if (scene.value.cards.size()>16 || scene.value.fingerprint.size()>8192) throw std::runtime_error("Quest scan observations exceed limits");
        if (stage_==Stage::closing) {
            if (scene.value.panel_visible || scene.value.scroll) {
                if (view.context.now<deadline_) return wait("Confirming quest log closed");
                if (scene.value.toggle && close_attempts_<2) {
                    ++close_attempts_;
                    return use(scene,*scene.value.toggle,0,"Retrying quest log close");
                }
                if (view.context.now>=deadline_) throw automation::ObservationUnavailable("Quest log did not confirm closing");
                return wait("Confirming quest log closed");
            }
            opened_=false;
            if (!deferred_reason_.empty()) return defer(scene,deferred_reason_);
            return finish(scene,view);
        }
        if (!scene.value.scroll || scene.value.fingerprint.empty()) {
            if (stage_==Stage::initial && scene.value.toggle && !scene.value.panel_visible) {
                stage_=Stage::opening; opened_=true; ++open_attempts_;
                return use(scene,*scene.value.toggle,0,"Opening quest log");
            }
            if (stage_==Stage::opening && scene.value.toggle && !scene.value.panel_visible && !scene.value.scroll) {
                if (view.context.now<deadline_) return wait("Confirming quest log opened");
                if (open_attempts_>=2) return defer(scene,"Quest scan deferred: the log did not open");
                ++open_attempts_;
                return use(scene,*scene.value.toggle,0,"Retrying quest log");
            }
            if (view.context.now>=deadline_) return defer(scene,"Quest scan deferred: the log could not be read");
            return wait("Confirming quest log contents");
        }
        if (stage_==Stage::initial && close_when_finished_) opened_=true;
        remember(scene);
        if (!requested_.empty() && std::all_of(requested_.begin(),requested_.end(),[&](const std::string& giver) {
            return std::any_of(confirmed_.begin(),confirmed_.end(),[&](const auto& item) {
                const auto& card=item.second.value;
                return card.giver==giver && card.complete_view && std::all_of(card.objectives.begin(),card.objectives.end(),
                    [](const auto& row) { return row.known; });
            });
        })) return finish(scene,view);
        if (stage_==Stage::initial || stage_==Stage::opening) stage_=Stage::up;
        if (!before_.empty()) {
            const bool unchanged=same_page(scene.value.fingerprint,before_);
            unchanged_=unchanged?unchanged_+1:0;
            if (!unchanged) ++report_.pages;
        } else ++report_.pages;
        if (unchanged_>=2) {
            if (stage_==Stage::down) { reached_end_=true; return finish(scene,view); }
            stage_=Stage::down; unchanged_=0;
        }
        if (scrolls_>=30 || view.context.now-began_>=Time(50000)) return finish(scene,view);
        before_=scene.value.fingerprint; ++scrolls_;
        if (!scroll_anchor_) scroll_anchor_=scene.value.scroll;
        return use(scene,*scroll_anchor_,stage_==Stage::up?4:-4,stage_==Stage::up?"Finding start of quest log":"Reading quest log");
    }
    void step_completed(const automation::View& view) override {
        if (!action_) return;
        action_=false; minimum_=view.context.now+Time(100); deadline_=view.context.now+Time(5000);
    }
    void finished(automation::State state) override {
        if (state!=automation::State::completed && state!=automation::State::deferred) { report_.finished=false; report_.status="Quest scan interrupted; results may be partial"; results_->publish(report_); }
    }
};
}
std::unique_ptr<automation::Program> browser_program(std::shared_ptr<const BrowserSource> source,std::shared_ptr<ScanResults> results,bool close_when_finished,std::set<std::string> requested) {
    if (!source || !results) throw std::invalid_argument("Quest browser requires observations and results");
    return std::make_unique<Browser>(std::move(source),std::move(results),close_when_finished,std::move(requested));
}
}
