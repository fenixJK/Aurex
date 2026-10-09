#pragma once
#include "module.hpp"
#include "browser.hpp"
#include "game/detection/detection.hpp"

namespace hive::features::quests {
struct RowRegion { std::string id; Rect area; };
struct CardRegion {
    std::string giver, quest;
    Rect title;
    std::vector<RowRegion> rows;
    bool complete_view{};
};
class LayoutSource {
public:
    virtual ~LayoutSource()=default;
    virtual std::vector<CardRegion> locate(const game::DetectionInput& input) const=0;
};
struct Cards final : game::Detection { std::vector<Observation<Card>> values; std::optional<BrowserScene> browser; };
Observation<Card> read_card(const game::DetectionInput& input,const CardRegion& region);
std::shared_ptr<const game::Detector> detector(std::shared_ptr<const LayoutSource> layout,std::vector<Field> fields);
std::shared_ptr<const Source> detection_source(std::shared_ptr<const game::DetectionSource> source);
std::shared_ptr<const LayoutSource> automatic_layout();
std::optional<Rect> quest_progress_area(const game::DetectionInput& input);
bool quest_names_complete(const game::DetectionInput& input,const std::vector<CardRegion>& regions);
bool quest_panel_visible(const game::DetectionInput& input);
std::vector<Field> default_fields();
BrowserScene locate_browser(const game::DetectionInput& input,const std::vector<CardRegion>& regions);
std::shared_ptr<const game::Detector> desktop_detector(std::vector<Field> fields);
std::shared_ptr<const BrowserSource> browser_source(std::shared_ptr<const game::DetectionSource> source);
}
