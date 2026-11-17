#include "conversation.hpp"
#include "game/detection/menus.hpp"

namespace hive::automation {
namespace {
class Conversation final : public Program {
    std::shared_ptr<const game::ConversationSource> source_;
    enum class Stage { prompt,opening,dialogue,closed } stage_{Stage::prompt};
    Time minimum_{},deadline_{},frame_{Time(-1)};
    bool started_{},issued_{};
    unsigned clicks_{};
public:
    explicit Conversation(std::shared_ptr<const game::ConversationSource> source)
        :source_(std::move(source)) {
        if (!source_) throw std::invalid_argument("Conversation needs observations");
    }
    RunConfig config() const override { return {Time(180000),28,{},true,true,true}; }
    Step next(const View& view) override {
        const auto& c=view.context;
        if (stage_==Stage::closed) return Done{"NPC conversation closed"};
        if (!started_) { started_=true; minimum_=c.now; deadline_=c.now+Time(10000); }
        if (c.now>=deadline_) throw ObservationUnavailable("NPC conversation did not confirm its next state");
        if (!view.observations) return Wait{};
        const auto scene=source_->read(c);
        if (!scene.has_value(c,Time(2000),minimum_) || scene.captured<=frame_) return Wait{};
        frame_=scene.captured;
        const auto& banner=view.observations->interaction_banner;
        const bool nearby=banner.has_value(c,Time(2000),minimum_) && banner.value==game::InteractionBanner::ready;
        if (stage_==Stage::prompt) {
            if (!scene.value.visible) {
                if (!nearby) return Wait{Time(50),"Waiting for NPC interaction"};
                issued_=true; stage_=Stage::opening;
                return Hold{{Key::interact},Time(80)};
            }
        }
        if (scene.value.visible) {
            stage_=Stage::dialogue;
            if (!scene.value.next) return Wait{Time(50),"Waiting for dialogue control"};
            if (++clicks_>120) throw std::runtime_error("NPC conversation exceeded its page limit");
            issued_=true;
            // Keep the pointer on the conversation until it closes. Every
            // click still needs a new, post-input observation of the control.
            return UseControl{{Knowledge::present,*scene.value.next,scene.captured,scene.window,scene.generation},0,"Continue NPC dialogue",*scene.value.next};
        }
        if (stage_==Stage::dialogue && nearby) {
            stage_=Stage::closed;
            return Hold{{},Time(40),game::pointer_rest(c,*view.observations)};
        }
        return Wait{Time(50),"Waiting for NPC conversation"};
    }
    void step_completed(const View& view) override {
        if (!issued_) return;
        issued_=false; minimum_=view.context.now+Time(1);
        deadline_=view.context.now+Time(10000);
    }
};
}
std::unique_ptr<Program> converse(std::shared_ptr<const game::ConversationSource> source) {
    return std::make_unique<Conversation>(std::move(source));
}
}
