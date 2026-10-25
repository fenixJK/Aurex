#include "items.hpp"
#include "game/data/stations.hpp"

namespace hive::features::collection {
namespace {
class StickerStack final : public automation::Program {
    enum class Phase { open, menu, purchase, submitted, closed } phase_{Phase::open};
    std::shared_ptr<const game::DetectionSource> source_;
    std::shared_ptr<ItemResult> result_;
    Time minimum_{},deadline_{};
    std::optional<Time> duration_;
    bool input_{};
    std::string outcome_;
public:
    StickerStack(std::shared_ptr<const game::DetectionSource> source,std::shared_ptr<ItemResult> result)
        :source_(std::move(source)),result_(std::move(result)) {
        if (!source_ || !result_) throw std::invalid_argument("Sticker Stack needs menu observations and a result");
        *result_={};
    }
    automation::RunConfig config() const override { return {Time(45000),28,{},true,true}; }
    game::ObservationNeeds observation_needs() const override { return phase_==Phase::purchase || phase_==Phase::submitted?game::ObservationNeeds{game::ObservationKind::confirmation}:game::ObservationNeeds{}; }
    ActivityStatus activity_status() const override { return {"Activating Sticker Stack","25 tickets"}; }
    std::vector<Rect> text_regions() const override {
        return phase_==Phase::purchase || phase_==Phase::submitted?std::vector<Rect>{{.1,.1,.85,.85}}:std::vector<Rect>{};
    }
    automation::Step next(const automation::View& view) override {
        using namespace automation;
        const auto& c=view.context;
        if (phase_==Phase::closed) return Done{outcome_};
        if (phase_==Phase::open) {
            phase_=Phase::menu; input_=true; minimum_=c.now; deadline_=c.now+Time(10000);
            return Hold{{Key::interact},Time(50)};
        }
        const auto packet=source_->read();
        const bool fresh=packet && packet->error.empty()
            && Observation<bool>{Knowledge::present,true,packet->context.now,packet->context.window,packet->context.generation}.has_value(c,Time(2000),minimum_);
        const auto menu=fresh?std::dynamic_pointer_cast<const game::ItemMenu>(packet->value):nullptr;
        const auto click=[&](Point point,Time captured,std::string label)->Step {
            input_=true;
            return UseControl{{Knowledge::present,point,captured,c.window,c.generation},0,std::move(label),Point{.85,.15}};
        };
        const auto close=[&](std::string message)->Step {
            outcome_=std::move(message); phase_=Phase::closed;
            if (menu && menu->kind==game::ItemMenuKind::sticker_stack && menu->close)
                return click(*menu->close,packet->context.now,"Close Sticker Stack");
            return Done{outcome_};
        };
        if (menu && !menu->error.empty()) {
            result_->retry=std::chrono::hours(1);
            return close("Sticker Stack unavailable: "+menu->error);
        }
        if (view.observations) {
            const auto& s=*view.observations;
            const auto missing=game::read_prompt_landmark(c,s.text,
                {{"not enough tickets","don't have enough tickets","do not have enough tickets"},{.1,.1,.85,.85},.7f,game::PromptMatch::phrase});
            if (missing.has_value(c,Time(2000),minimum_)) {
                result_->retry=std::chrono::hours(1);
                return close("Sticker Stack needs 25 tickets");
            }
            if (phase_==Phase::purchase) {
                const auto& dialog=s.confirmation;
                if (dialog.has_value(c,Time(2000),minimum_)) {
                    phase_=Phase::submitted;
                    return click(dialog.value.yes,dialog.captured,"Activate Sticker Stack for 25 tickets");
                }
            }
            if (phase_==Phase::submitted && menu) {
                const auto remaining=game::read_text_cooldown(c,s,game::station_target("Sticker Stack",true),minimum_);
                const bool dismissed=menu->kind==game::ItemMenuKind::none
                    && s.confirmation.knowledge==Knowledge::absent && s.confirmation.usable(c,Time(2000),minimum_);
                if (remaining.has_value(c,Time(2000)) || dismissed) {
                    result_->cooldown=remaining.has_value(c,Time(2000))?remaining.value:*duration_;
                    return close("Sticker Stack activated");
                }
            }
        }
        if (c.now>=deadline_) {
            if (phase_==Phase::submitted) {
                result_->retry=duration_;
                return close("Sticker Stack activation could not be confirmed; waiting before another purchase");
            }
            throw ObservationUnavailable("Sticker Stack menu could not be read");
        }
        if (phase_==Phase::menu && menu && menu->kind==game::ItemMenuKind::sticker_stack
            && menu->submit && menu->boost_duration) {
            duration_=menu->boost_duration; phase_=Phase::purchase;
            return click(*menu->submit,packet->context.now,"Choose 25 tickets");
        }
        return Wait{Time(100),"Waiting for Sticker Stack"};
    }
    void step_completed(const automation::View& view) override {
        if (input_) { input_=false; minimum_=view.context.now+Time(1); deadline_=view.context.now+Time(10000); }
    }
};
}
std::unique_ptr<automation::Program> activate_sticker_stack(std::shared_ptr<const game::DetectionSource> source,
    std::shared_ptr<ItemResult> result) {
    return std::make_unique<StickerStack>(std::move(source),std::move(result));
}
}
