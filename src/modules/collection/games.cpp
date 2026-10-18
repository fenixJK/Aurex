#include "games.hpp"
#include "card_memory.hpp"

namespace hive::features::collection {
namespace {
class MemoryMatch final : public automation::Program {
    std::shared_ptr<const game::DetectionSource> source_;
    std::string station_;
    CardMemory memory_;
    bool entered_{},accepted_{},playing_{};
    unsigned retries_{};
    Time clicked_{},began_{},last_board_{Time(-1)};
    std::optional<unsigned> pending_;
public:
    MemoryMatch(std::shared_ptr<const game::DetectionSource> source,std::string station):source_(std::move(source)),station_(std::move(station)) {
        if (!source_ || station_.empty()) throw std::invalid_argument("Memory Match needs board observations and a station");
    }
    automation::RunConfig config() const override { return {std::chrono::minutes(3),28,{},true,true,true}; }
    ActivityStatus activity_status() const override { return {"Playing",station_}; }
    automation::Step next(const automation::View& view) override {
        using namespace automation;
        const auto& c=view.context;
        const auto packet=source_->read();
        std::shared_ptr<const game::MemoryBoard> board;
        if (packet && packet->error.empty()) {
            const Observation<bool> fresh{Knowledge::present,true,packet->context.now,packet->context.window,packet->context.generation};
            if (fresh.has_value(c,Time(2000),began_)) board=std::dynamic_pointer_cast<const game::MemoryBoard>(packet->value);
        }
        if (!entered_) { entered_=true; began_=c.now; if (!board || board->cards.empty()) return Hold{{Key::interact},Time(100),{}}; }
        if (c.now-began_>=std::chrono::minutes(2)) throw ObservationUnavailable("Memory Match did not finish");
        if (view.observations) {
            const auto& banner=view.observations->interaction_banner;
            if (playing_ && board && board->visibility==Knowledge::absent && packet->context.now>clicked_
                && banner.has_value(c,Time(2000),began_) && banner.value==game::InteractionBanner::cooldown) return Done{"Finished "+station_};
            if (!playing_ && !accepted_) {
                const auto& dialog=view.observations->confirmation;
                if (dialog.has_value(c,Time(2000),began_)) {
                    accepted_=true;
                    return UseControl{{Knowledge::present,dialog.value.yes,dialog.captured,dialog.window,dialog.generation},0,"Start "+station_,Point{.85,.15}};
                }
            }
        }
        if (!playing_ && c.now-began_>Time(10000)) throw ObservationUnavailable("Memory Match board did not open");
        if (!board || board->cards.empty() || packet->context.now<=last_board_) return Wait{};
        if (pending_ && packet->context.now<clicked_+Time(300)) return Wait{};
        last_board_=packet->context.now;
        if (!playing_) { memory_.begin(*board); playing_=true; }
        else memory_.observe(*board);
        if (memory_.waiting()) {
            if (c.now-clicked_<Time(1500)) return Wait{};
            if (++retries_>1) throw ObservationUnavailable("Memory card did not turn over");
            memory_.retry_click();
        } else { pending_.reset(); retries_=0; }
        const auto choice=pending_?pending_:memory_.choose();
        if (!choice) return Wait{Time(100),"Waiting for cards"};
        pending_=choice; memory_.clicked(*choice); clicked_=c.now;
        return UseControl{{Knowledge::present,memory_.center(*choice),packet->context.now,c.window,c.generation},0,"Turn card",Point{.85,.15}};
    }
};
}
std::unique_ptr<automation::Program> memory_match(std::shared_ptr<const game::DetectionSource> source,std::string station) {
    return std::make_unique<MemoryMatch>(std::move(source),std::move(station));
}
}
