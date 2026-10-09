#include "inventory.hpp"
#include "game/detection/menus.hpp"
#include <algorithm>
#include <sstream>

namespace hive::workflows {
void validate(const InventoryDragPlan& plan) {
    if (plan.names.empty() || plan.names.size()>4 || (!plan.inspect_only && plan.target.empty()) || plan.target.size()>120 || plan.timeout<Time(1000) || plan.timeout>Time(120000)
        || plan.drag<Time(50) || plan.drag>Time(250) || !plan.max_scrolls || plan.max_scrolls>100 || (plan.current_page_only && !plan.inspect_only)) throw std::invalid_argument("Invalid inventory drag plan");
    std::set<std::string> names;
    for (const auto& name : plan.names) if (game::inventory_name(name).empty() || !names.insert(game::inventory_name(name)).second)
        throw std::invalid_argument("Invalid or duplicate inventory item name");
}
namespace {
std::string signature(const game::InventoryView& view,const game::InventoryItem* selected=nullptr) {
    std::ostringstream out;
    out<<view.open<<':'<<view.target;
    if (!selected) out<<':'<<view.readable<<':'<<view.at_top<<':'<<view.at_bottom<<':'<<view.viewport.x<<':'<<view.viewport.y<<':'<<view.viewport.width<<':'<<view.viewport.height;
    if (view.drop) out<<':'<<view.drop->x<<':'<<view.drop->y; else out<<":none";
    for (const auto& item : view.items) {
        if (selected && &item!=selected) continue;
        out<<':'<<game::inventory_name(item.name)<<':'<<item.bounds.x<<':'<<item.bounds.y<<':'<<item.bounds.width<<':'<<item.bounds.height<<':'<<item.grab.x<<':'<<item.grab.y;
        if (selected && item.quantity) {
            out<<":count="<<item.quantity->count;
            if (item.quantity->capacity) out<<'/'<<*item.quantity->capacity;
        } else out<<":count=unknown";
    }
    return out.str();
}
}
InventoryDrag::InventoryDrag(InventoryDragPlan plan):plan_(std::move(plan)) { validate(plan_); }
void InventoryDrag::start(const ObservationContext& c) {
    if (stage_!=InventoryStage::idle || !c.generation || !c.window.id || !c.window.bounds.valid() || c.now<Time::zero() || c.now>Time::max()-plan_.timeout)
        throw std::invalid_argument("Inventory search needs a new valid session");
    started_=c; minimum_=last_=c.now; stage_=InventoryStage::searching; reason_="Reading inventory";
    seek_bottom_=plan_.start_at_bottom && !plan_.current_page_only;
}
void InventoryDrag::finish(InventoryStage stage,std::string reason,TaskPort& tasks,ModuleScope scope) {
    stage_=stage; reason_=std::move(reason);
    try { tasks.cancel(scope); tasks.results(scope); tasks.release(scope); }
    catch (const std::exception& e) { stage_=InventoryStage::failed; reason_+="; "+std::string(e.what()); }
    pending_.clear(); owned_=false;
}
void InventoryDrag::stop(TaskPort& tasks,ModuleScope scope) { finish(InventoryStage::stopped,"Inventory interaction stopped",tasks,scope); }
void InventoryDrag::update(const ObservationContext& c,const game::State& state,const Observation<game::InventoryView>& reading,TaskPort& tasks,ModuleScope scope) {
    if (stage_==InventoryStage::idle || stage_==InventoryStage::completed || stage_==InventoryStage::failed || stage_==InventoryStage::stopped) return;
    auto fail=[&](std::string reason) { finish(InventoryStage::failed,std::move(reason),tasks,scope); };
    if (c.now<last_ || c.generation!=started_.generation || scope.generation!=c.generation || c.window.id!=started_.window.id
        || c.window.process!=started_.window.process || c.window.bounds!=started_.window.bounds) { fail("Inventory context changed"); return; }
    last_=c.now;
    if (c.now-started_.now>=plan_.timeout) {
        if (plan_.current_page_only) finish(InventoryStage::completed,"Inventory page was inconclusive",tasks,scope);
        else fail("Could not "+std::string(stage_==InventoryStage::dragging?"drag ":"find ")+plan_.names.front()+" in inventory");
        return;
    }
    if ((state.connection.has_value(c,Time(2000),started_.now) && state.connection.value==game::Connection::disconnected)
        || (state.alive.has_value(c,Time(2000),started_.now) && !state.alive.value)) { fail("Game disconnected or player respawning"); return; }
    try {
        if (!owned_) {
            if (!tasks.acquire(scope)) return;
            owned_=true; minimum_=plan_.use_recent_view?std::max(Time{},c.now-Time(2000)):c.now;
        }
        for (const auto& result : tasks.results(scope)) if (result.id==pending_) {
            pending_.clear();
            if (result.outcome!=TaskOutcome::executed) { fail("Inventory input interrupted; check the game before retrying"); return; }
            if (stage_==InventoryStage::dragging) { finish(InventoryStage::completed,"Item drag finished; game outcome still needs confirmation",tasks,scope); return; }
            minimum_=result.finished+Time(1); frame_=Time(-1); stage_=InventoryStage::searching;
        }
        if (!pending_.empty()) return;
        if (!reading.has_value(c,Time(2000),minimum_)) return;
        game::validate(reading.value); const auto& view=reading.value;
        const game::InventoryItem* selected=nullptr;
        for (const auto& item : view.items) if (std::any_of(plan_.names.begin(),plan_.names.end(),[&](const auto& name) { return game::inventory_name(name)==game::inventory_name(item.name); })) {
            if (selected) { fail("Inventory item name is ambiguous"); return; } selected=&item;
        }
        if (reading.captured<=frame_) return;
        frame_=reading.captured; const auto current=signature(view,seek_bottom_ && scrolls_?nullptr:selected);
        first_seen_=reading.captured;
        if (!view.open) {
            if (opened_) { reason_="Waiting for inventory to open"; return; }
            if (!view.toggle) { reason_="Waiting for inventory control"; return; }
            const auto rest=game::pointer_rest(c,state);
            pending_=tasks.submit(scope,TaskClass::interaction,
                {{"Move to inventory",{},Time(50),view.toggle},{"Open inventory",{Key::primary},Time(50),view.toggle},
                    {"Move off inventory",{},Time(100),rest}},Time(2000));
            opened_=true; stage_=InventoryStage::opening; reason_="Opening inventory"; return;
        }
        auto scroll=[&](int amount) {
            if (scrolls_>=plan_.max_scrolls) { fail("Inventory scroll limit reached"); return; }
            const Point hover{view.viewport.x+view.viewport.width*0.5,view.viewport.y+view.viewport.height*0.5};
            last_scroll_=amount;
            pending_=tasks.submit(scope,TaskClass::interaction,{{"Scroll inventory",{},Time(150),hover,amount}},Time(2000));
            before_rows_.clear(); for (const auto& item:view.items) before_rows_.emplace(game::inventory_name(item.name),item.bounds.y);
            ++scrolls_; before_scroll_=current; stage_=InventoryStage::scrolling; reason_="Searching inventory";
        };
        if (seek_bottom_ && !selected) {
            if (!before_scroll_.empty()) {
                stalled_=current==before_scroll_?stalled_+1:0; before_scroll_.clear();
                moved_down_=moved_down_ || !stalled_;
            }
            if ((view.at_bottom && stalled_>0) || (stalled_>=2 && moved_down_)) {
                seek_bottom_=down_=false; stalled_=0; last_scroll_=0; before_rows_.clear();
            } else {
                if (stalled_>=2) { fail("Inventory did not move after scrolling"); return; }
                scroll(-8); return;
            }
        }
        if (selected) {
            if (plan_.inspect_only) {
                if (plan_.require_quantity && !selected->quantity) { reason_="Reading "+selected->name+" quantity"; return; }
                match_={Knowledge::present,{*selected,first_seen_},reading.captured,reading.window,reading.generation};
                finish(InventoryStage::completed,"Inventory item inspected",tasks,scope); return;
            }
            if (selected->quantity && !selected->quantity->count) {
                if (plan_.allow_absent) {
                    match_={Knowledge::absent,{},reading.captured,reading.window,reading.generation};
                    finish(InventoryStage::completed,"No available "+selected->name+" in inventory",tasks,scope);
                } else fail("No available "+selected->name+" in inventory");
                return;
            }
            if (!view.drop || view.target!=plan_.target) { reason_="Waiting for the item destination"; return; }
            match_={Knowledge::present,{*selected,first_seen_},reading.captured,reading.window,reading.generation};
            pending_=tasks.submit(scope,TaskClass::interaction,{
                {"Move to inventory item",{},Time(50),selected->grab},
                {"Drag inventory item",{Key::primary},plan_.drag,selected->grab,0,view.drop}},Time(2000));
            stage_=InventoryStage::dragging; reason_="Dragging "+selected->name; return;
        }
        if (plan_.current_page_only) {
            finish(InventoryStage::completed,"Item not confirmed on the visible inventory page",tasks,scope);
            return;
        }
        if (!view.readable && view.items.empty()) {
            if (unreadable_since_<Time{}) unreadable_since_=c.now;
            if (c.now-unreadable_since_>=Time(5000)) fail("Inventory titles could not be read; item availability is unknown");
            else reason_="Waiting for readable inventory rows";
            return;
        }
        unreadable_since_=Time(-1);
        int correction=0;
        if (!before_scroll_.empty()) {
            stalled_=current==before_scroll_?stalled_+1:0; before_scroll_.clear();
            if (!stalled_) {
                if (last_scroll_<0) moved_down_=true;
                else if (last_scroll_>0) moved_up_=true;
            }
            const bool overlap=std::any_of(view.items.begin(),view.items.end(),[&](const auto& item) { return before_rows_.contains(game::inventory_name(item.name)); });
            if (!overlap && !backtracking_) {
                if (scroll_size_==1) { fail("Inventory scrolling skipped unread rows"); return; }
                correction=last_scroll_>0?1-last_scroll_:-1-last_scroll_; scroll_size_=1;
            } else if (view.readable && !backtracking_ && !stalled_) {
                std::vector<double> shifts;
                double row_height=0;
                for (const auto& item:view.items) {
                    row_height=std::max(row_height,item.bounds.height);
                    const auto old=before_rows_.find(game::inventory_name(item.name));
                    if (old==before_rows_.end()) continue;
                    const auto distance=(item.bounds.y-old->second)*(last_scroll_>0?1:-1);
                    if (distance>view.viewport.height*.01) shifts.push_back(distance/std::abs(last_scroll_));
                }
                if (!shifts.empty()) {
                    std::sort(shifts.begin(),shifts.end());
                    scroll_size_=std::clamp(int(std::floor(std::max(row_height,view.viewport.height-2*row_height)/shifts[shifts.size()/2])),1,8);
                }
            }
        }
        if (down_ && last_scroll_<0 && stalled_>=2) {
            if (!moved_down_ && !view.at_bottom) { fail("Inventory did not move after scrolling"); return; }
            down_=false; incomplete_scan_=false; stalled_=0;
        } else if (view.at_bottom && last_scroll_<0 && stalled_>0) { down_=false; incomplete_scan_=false; stalled_=0; }
        if (!down_ && !view.readable) incomplete_scan_=true;
        if (!correction && !down_ && last_scroll_>0 && (view.at_top && stalled_>0 || stalled_>=2 && moved_up_)) {
            if (incomplete_scan_) { fail("Some inventory titles could not be read; item availability is unknown"); return; }
            if (plan_.inspect_only || plan_.allow_absent) {
                match_={Knowledge::absent,{},reading.captured,reading.window,reading.generation};
                finish(InventoryStage::completed,"Item was not found in the readable inventory",tasks,scope);
            } else fail("Item was not found in the readable inventory");
            return;
        }
        if (!view.readable) scroll_size_=1;
        backtracking_=correction!=0;
        scroll(correction?correction:(down_?-scroll_size_:scroll_size_));
    } catch (const std::exception& e) { fail(e.what()); }
}
}
