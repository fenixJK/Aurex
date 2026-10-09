#include "orders.hpp"
#include <algorithm>
#include <set>

namespace hive::features::collection {
namespace {
using game::ItemMenuKind;
const ItemQueue& queue(const ItemOrderState& state,ItemMenuKind kind) {
    if (kind==ItemMenuKind::blender) return state.blender;
    if (kind==ItemMenuKind::wind_shrine) return state.shrine;
    throw std::invalid_argument("Unknown item queue");
}
std::optional<ItemOrder> next(const ItemQueue& q,game::WallTime now) {
    const auto last=std::find_if(q.orders.begin(),q.orders.end(),[&](const auto& p) { return p.order.item==q.last; });
    const auto start=last==q.orders.end()?0:std::size_t(last-q.orders.begin()+1);
    for (std::size_t i=0;i<q.orders.size();++i) {
        const auto& p=q.orders[(start+i)%q.orders.size()];
        if ((p.order.unlimited || p.completed<p.order.runs) && p.blocked_until<=now) return p.order;
    }
    return {};
}
void check_order(const ItemOrder& order,ItemMenuKind kind) {
    const auto& items=kind==ItemMenuKind::blender?game::blender_recipes():game::shrine_items();
    if (std::find(items.begin(),items.end(),order.item)==items.end() || !order.amount || order.amount>999 || !order.runs || order.runs>999)
        throw std::invalid_argument("Invalid item order");
}
}
void validate(const ItemOrderState& state) {
    for (const auto kind:{ItemMenuKind::blender,ItemMenuKind::wind_shrine}) {
        const auto& q=queue(state,kind); std::set<std::string> seen;
        if (q.orders.size()>64 || q.last.size()>80 || q.ready.time_since_epoch()<Time{}) throw std::invalid_argument("Invalid item queue");
        for (const auto& p:q.orders) {
            check_order(p.order,kind);
            if (!seen.insert(p.order.item).second || p.blocked_until.time_since_epoch()<Time{} || p.completed>1000000000)
                throw std::invalid_argument("Invalid order progress");
        }
        if (q.crafting) { if (kind!=ItemMenuKind::blender) throw std::invalid_argument("Shrine cannot be crafting"); check_order(*q.crafting,kind); }
    }
}
ItemOrders::ItemOrders(std::shared_ptr<ItemOrderStore> store,game::WallClock clock):store_(std::move(store)),clock_(std::move(clock)) {
    if (!clock_) clock_=[] { return std::chrono::time_point_cast<Time>(std::chrono::system_clock::now()); };
}
void ItemOrders::load() const {
    if (loaded_) return;
    const auto state=store_?store_->load():ItemOrderState{};
    validate(state); state_=state; loaded_=true;
}
ItemQueue& ItemOrders::queue(ItemMenuKind kind) { return const_cast<ItemQueue&>(collection::queue(state_,kind)); }
void ItemOrders::save(const ItemOrderState& previous) {
    try { validate(state_); if (store_) store_->save(state_); }
    catch (...) { state_=previous; throw; }
}
void ItemOrders::configure(ItemMenuKind kind,const std::vector<ItemOrder>& orders) {
    std::lock_guard lock(mutex_); load(); const auto previous=state_; auto& q=queue(kind);
    std::vector<OrderProgress> next;
    for (const auto& order:orders) {
        check_order(order,kind);
        const auto old=std::find_if(q.orders.begin(),q.orders.end(),[&](const auto& p) { return p.order.item==order.item; });
        if (old!=q.orders.end() && old->order.amount==order.amount) { next.push_back(*old); next.back().order=order; }
        else next.push_back({order});
    }
    q.orders=std::move(next); save(previous);
}
std::optional<ItemOrder> ItemOrders::next(ItemMenuKind kind) const {
    std::lock_guard lock(mutex_); load(); return collection::next(collection::queue(state_,kind),clock_());
}
bool ItemOrders::pending(ItemMenuKind kind) const {
    std::lock_guard lock(mutex_); load(); const auto& q=collection::queue(state_,kind); const auto now=clock_();
    return q.ready<=now && (q.crafting.has_value() || collection::next(q,now).has_value());
}
void ItemOrders::accepted(ItemMenuKind kind,const ItemOrder& order,Time delay) {
    if (delay<Time{} || delay>std::chrono::days(7)) throw std::invalid_argument("Invalid item delay");
    std::lock_guard lock(mutex_); load(); const auto previous=state_; auto& q=queue(kind);
    const auto p=std::find_if(q.orders.begin(),q.orders.end(),[&](const auto& p) { return p.order==order; });
    if (p==q.orders.end() || (!order.unlimited && p->completed>=order.runs)) throw std::logic_error("Item order changed during its visit");
    p->completed=std::min(1000000000u,p->completed+1); p->blocked_until={};
    q.last=order.item; q.ready=clock_()+delay;
    if (kind==ItemMenuKind::blender) q.crafting=order;
    save(previous);
}
void ItemOrders::collected() {
    std::lock_guard lock(mutex_); load(); if (!state_.blender.crafting) return;
    const auto previous=state_; state_.blender.crafting.reset(); state_.blender.ready={}; save(previous);
}
void ItemOrders::defer(ItemMenuKind kind,Time delay) {
    if (delay<=Time{} || delay>std::chrono::days(7)) throw std::invalid_argument("Invalid item retry delay");
    std::lock_guard lock(mutex_); load(); const auto previous=state_; queue(kind).ready=clock_()+delay; save(previous);
}
void ItemOrders::missing(ItemMenuKind kind,const std::string& item) {
    std::lock_guard lock(mutex_); load(); const auto previous=state_; auto& q=queue(kind);
    for (auto& p:q.orders) if (p.order.item==item) p.blocked_until=clock_()+std::chrono::hours(1);
    q.last=item; save(previous);
}
void ItemOrders::reset(ItemMenuKind kind) {
    std::lock_guard lock(mutex_); load(); const auto previous=state_; auto& q=queue(kind);
    for (auto& p:q.orders) { p.completed=0; p.blocked_until={}; }
    q.last.clear(); save(previous);
}
ItemOrderState ItemOrders::read() const { std::lock_guard lock(mutex_); load(); return state_; }
}
