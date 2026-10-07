#pragma once
#include "hive/game/detection/memory_match.hpp"

namespace hive::features::collection {
class CardMemory {
    struct Slot { Rect bounds; game::CardImage back; std::optional<game::CardImage> face; bool matched{}; };
    std::vector<Slot> slots_;
    std::vector<unsigned> open_;
    std::optional<unsigned> waiting_;
public:
    void begin(const game::MemoryBoard&);
    void observe(const game::MemoryBoard&);
    std::optional<unsigned> choose() const;
    void clicked(unsigned);
    void retry_click() { waiting_.reset(); }
    Point center(unsigned) const;
    bool waiting() const { return waiting_.has_value(); }
    bool complete() const;
};
}
