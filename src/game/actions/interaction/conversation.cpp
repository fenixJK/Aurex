#include "conversation.hpp"

namespace hive::automation {
namespace {
class Conversation final : public Program {
    game::PromptLandmark prompt_;
    std::shared_ptr<const game::ConversationSource> source_;
    enum class Stage { prompt,opening,dialogue } stage_{Stage::prompt};
    Time minimum_{},deadline_{},frame_{Time(-1)};
    bool started_{},issued_{};
    unsigned clicks_{};
public:
    Conversation(game::PromptLandmark prompt,std::shared_ptr<const game::ConversationSource> source)
        :prompt_(std::move(prompt)),source_(std::move(source)) {
        game::validate(prompt_); if (!source_) throw std::invalid_argument("Conversation needs observations");
    }
    RunConfig config() const override { return {Time(180000),28,{},true,true}; }
    Step next(const View& view) override {
        const auto& c=view.context;
        if (!started_) { started_=true; minimum_=c.now; deadline_=c.now+Time(10000); }
        if (c.now>=deadline_) throw std::runtime_error("NPC conversation did not confirm its next state");
        if (!view.observations) return Wait{};
        const auto scene=source_->read(c);
        if (!scene.has_value(c,Time(2000),minimum_) || scene.captured<=frame_) return Wait{};
        frame_=scene.captured;
        const auto prompt=game::read_prompt_landmark(c,view.observations->text,prompt_);
        const bool nearby=prompt.has_value(c,Time(2000),minimum_) && prompt.value;
        if (stage_==Stage::prompt) {
            if (scene.value.visible) throw std::runtime_error("Close the existing conversation before starting another");
            if (!nearby) return Wait{Time(50),"Waiting for NPC interaction"};
            issued_=true; stage_=Stage::opening;
            return UsePrompt{prompt_,{{"Click to continue"},{.15,.5,.7,.5},.8f,game::PromptMatch::phrase},Time(10000)};
        }
        if (scene.value.visible) {
            stage_=Stage::dialogue;
            if (!scene.value.next) return Wait{Time(50),"Waiting for dialogue control"};
            if (++clicks_>120) throw std::runtime_error("NPC conversation exceeded its page limit");
            issued_=true;
            return UseControl{{Knowledge::present,*scene.value.next,scene.captured,scene.window,scene.generation},0,"Continue NPC dialogue",Point{.9,.4}};
        }
        if (stage_==Stage::dialogue && nearby) return Done{"NPC conversation closed"};
        return Wait{Time(50),"Waiting for NPC conversation"};
    }
    void step_completed(const View& view) override {
        if (!issued_) return;
        issued_=false; minimum_=view.context.now+Time(250);
        deadline_=view.context.now+Time(10000);
    }
};
}
std::unique_ptr<Program> converse(game::PromptLandmark prompt,std::shared_ptr<const game::ConversationSource> source) {
    return std::make_unique<Conversation>(std::move(prompt),std::move(source));
}
}
