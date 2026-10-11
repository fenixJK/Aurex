#pragma once
#include "detection.hpp"

namespace hive::game {
enum class ItemMenuKind { none, blender, wind_shrine, sticker_printer, sticker_stack };
enum class ItemMenuStage { unknown, selection, quantity, crafting };
struct ItemMenu final : Detection {
    ItemMenuKind kind{ItemMenuKind::none};
    ItemMenuStage stage{ItemMenuStage::unknown};
    std::string item,dialogue,error;
    std::vector<Point> choices;
    std::optional<unsigned> quantity;
    std::optional<Point> previous,next,less,more,submit,cancel,close,continue_dialogue;
    std::optional<bool> enabled,finished;
    std::optional<Time> remaining;
    std::optional<Time> boost_duration;
};
const std::vector<std::string>& blender_recipes();
const std::vector<std::string>& shrine_items();
std::shared_ptr<const Detector> item_menu_detector();
}
