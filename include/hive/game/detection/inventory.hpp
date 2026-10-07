#pragma once
#include "detection.hpp"
#include "state.hpp"

namespace hive::game {
struct InventoryRow { Rect bounds,title; Point grab; };
struct InventoryLayout {
    bool open{},at_top{},at_bottom{};
    Rect viewport;
    std::optional<Point> toggle,drop;
    std::string target;
    std::vector<InventoryRow> rows;
};
class InventoryLayoutSource {
public:
    virtual ~InventoryLayoutSource()=default;
    virtual Observation<InventoryLayout> read(const DetectionInput& input) const=0;
};
struct InventoryQuantity {
    std::uint64_t count{};
    std::optional<std::uint64_t> capacity;
    bool operator==(const InventoryQuantity&) const=default;
};
struct InventoryItem { std::string name; Rect bounds; Point grab; std::optional<InventoryQuantity> quantity; };
struct InventoryView {
    bool open{},at_top{},at_bottom{},readable{};
    Rect viewport;
    std::optional<Point> toggle,drop;
    std::string target;
    std::vector<InventoryItem> items;
};
struct InventoryDetection final : Detection { Observation<InventoryView> view; };
class InventorySource {
public:
    virtual ~InventorySource()=default;
    virtual Observation<InventoryView> read(const ObservationContext&,const State&) const=0;
};
std::string inventory_name(const std::string& text);
void validate(const InventoryView& view);
Observation<InventoryView> read_inventory(const DetectionInput& input,const Observation<InventoryLayout>& layout);
std::shared_ptr<const Detector> inventory_detector(std::shared_ptr<const InventoryLayoutSource> layout);
std::shared_ptr<const InventorySource> inventory_source(std::shared_ptr<const DetectionSource> channel);
Observation<InventoryLayout> locate_inventory(const DetectionInput& input);
std::shared_ptr<const InventoryLayoutSource> inventory_layout();
std::vector<InventoryRow> inventory_rows(const Frame& frame,Rect viewport);
}
