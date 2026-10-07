#pragma once
#include "hive/platform/platform.hpp"

namespace hive::game {
class RecognitionCache {
    struct Entry { Rect area; std::vector<std::uint8_t> pixels; std::vector<TextLine> lines; Time recognized{}; };
    std::vector<Entry> entries_;
    ObservationContext context_;
    int width_{},height_{};
public:
    void clear();
    std::vector<TextLine> read(Desktop& desktop,const Frame& frame,std::uint64_t generation,
        std::vector<Rect> regions,const std::function<bool()>& cancelled={});
};
}
