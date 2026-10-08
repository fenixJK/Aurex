#pragma once
#include "hive/game/detection/inventory.hpp"
#include "hive/core/tasks.hpp"
#include <map>

namespace hive::workflows {
struct InventoryDragPlan {
    std::vector<std::string> names;
    std::string target;
    Time timeout{60000},drag{150};
    unsigned max_scrolls{40};
    bool inspect_only{},current_page_only{},allow_absent{},start_at_bottom{};
    bool use_recent_view{};
};
void validate(const InventoryDragPlan& plan);
enum class InventoryStage { idle, searching, opening, scrolling, dragging, completed, failed, stopped };
struct InventoryMatch { game::InventoryItem item; Time first_seen{}; };
class InventoryDrag {
    InventoryDragPlan plan_;
    ObservationContext started_;
    InventoryStage stage_{InventoryStage::idle};
    Time last_{},minimum_{},frame_{Time(-1)},unreadable_since_{Time(-1)};
    std::string pending_,reason_,before_scroll_;
    std::map<std::string,double> before_rows_;
    unsigned scrolls_{},stalled_{};
    int scroll_size_{1},last_scroll_{};
    bool owned_{},opened_{},down_{true},backtracking_{},incomplete_scan_{},moved_down_{},moved_up_{},seek_bottom_{};
    Time first_seen_{};
    Observation<InventoryMatch> match_;
    void finish(InventoryStage stage,std::string reason,TaskPort& tasks,ModuleScope scope);
public:
    explicit InventoryDrag(InventoryDragPlan plan);
    void start(const ObservationContext& context);
    void update(const ObservationContext& context,const game::State& state,const Observation<game::InventoryView>& view,TaskPort& tasks,ModuleScope scope);
    void stop(TaskPort& tasks,ModuleScope scope);
    const Observation<InventoryMatch>& match() const { return match_; }
    InventoryStage stage() const { return stage_; }
    const std::string& reason() const { return reason_; }
};
}
