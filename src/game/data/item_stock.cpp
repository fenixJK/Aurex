#include "item_stock.hpp"
#include <algorithm>

namespace hive::game {
namespace {
void validate(const ItemAmounts& amounts) {
    if (amounts.size()>128) throw std::invalid_argument("Too many inventory items");
    for (const auto& [item,n]:amounts) if (item.empty() || item.size()>80 || n>1000000000)
        throw std::invalid_argument("Invalid item quantity");
}
std::optional<std::string> blocked(const ItemStockView& state,const ItemAmounts& cost) {
    for (const auto& [item,n]:cost) {
        const auto reserve=state.reserves.find(item);
        if (!n || reserve==state.reserves.end() || !reserve->second) continue;
        const auto found=state.counts.find(item);
        if (found==state.counts.end() || found->second.count<reserve->second || n>found->second.count-reserve->second) return item;
    }
    return {};
}
}
void ItemStock::configure(ItemAmounts reserves) {
    validate(reserves); std::erase_if(reserves,[](const auto& entry) { return !entry.second; });
    std::lock_guard lock(mutex_); state_.reserves=std::move(reserves);
}
void ItemStock::observe(const std::string& item,std::uint64_t count,Time at) {
    if (item.empty() || item.size()>80 || at<Time{}) throw std::invalid_argument("Invalid inventory observation");
    std::lock_guard lock(mutex_);
    const auto found=state_.counts.find(item);
    if (found!=state_.counts.end() && found->second.updated>=at) return;
    if (found==state_.counts.end() && state_.counts.size()>=128) return;
    state_.counts[item]={count,at,false};
}
std::vector<std::string> ItemStock::unread(const ItemAmounts& cost) const {
    std::lock_guard lock(mutex_); std::vector<std::string> result;
    for (const auto& [item,n]:cost) if (n && state_.reserves.contains(item) && !state_.counts.contains(item)) result.push_back(item);
    return result;
}
std::optional<std::string> ItemStock::blocked(const ItemAmounts& cost) const {
    std::lock_guard lock(mutex_); return game::blocked(state_,cost);
}
bool ItemStock::spend(const ItemAmounts& cost,Time at) {
    validate(cost); if (at<Time{}) throw std::invalid_argument("Invalid item spend time");
    std::lock_guard lock(mutex_); if (game::blocked(state_,cost)) return false;
    for (const auto& [item,n]:cost) if (const auto found=state_.counts.find(item);found!=state_.counts.end()) {
        auto& count=found->second; count.count-=std::min(count.count,n); count.estimated=true; count.updated=std::max(at,count.updated);
    }
    return true;
}
ItemStockView ItemStock::read() const { std::lock_guard lock(mutex_); return state_; }
ItemAmounts blender_cost(const std::string& recipe,unsigned amount) {
    static const std::map<std::string,ItemAmounts> recipes{
        {"Red Extract",{{"Royal Jelly",10},{"Strawberry",50}}},
        {"Blue Extract",{{"Royal Jelly",10},{"Blueberry",50}}},
        {"Enzymes",{{"Royal Jelly",10},{"Pineapple",50}}},
        {"Oil",{{"Royal Jelly",10},{"Sunflower Seed",50}}},
        {"Glue",{{"Royal Jelly",10},{"Gumdrops",50}}},
        {"Tropical Drink",{{"Coconut",10},{"Enzymes",2},{"Oil",2}}},
        {"Gumdrops",{{"Pineapple",3},{"Strawberry",3},{"Blueberry",3}}},
        {"Moon Charms",{{"Royal Jelly",1},{"Pineapple",5},{"Gumdrops",5}}},
        {"Glitter",{{"Moon Charm",25},{"Magic Bean",1}}},
        {"Star Jelly",{{"Royal Jelly",100},{"Glitter",3}}},
        {"Purple Potion",{{"Neonberry",3},{"Red Extract",3},{"Blue Extract",3},{"Glue",3}}},
        {"Soft Wax",{{"Honeysuckle",5},{"Oil",1},{"Enzymes",1},{"Royal Jelly",10}}},
        {"Hard Wax",{{"Soft Wax",3},{"Enzymes",3},{"Bitterberry",33},{"Royal Jelly",33}}},
        {"Swirled Wax",{{"Hard Wax",3},{"Soft Wax",9},{"Purple Potion",6},{"Royal Jelly",3333}}},
        {"Caustic Wax",{{"Hard Wax",5},{"Enzymes",5},{"Neonberry",25},{"Royal Jelly",5252}}},
        {"Field Dice",{{"Soft Wax",1},{"Whirligig",1},{"Red Extract",1},{"Blue Extract",1}}},
        {"Smooth Dice",{{"Field Dice",3},{"Soft Wax",3},{"Whirligig",3},{"Oil",3}}},
        {"Loaded Dice",{{"Smooth Dice",3},{"Hard Wax",3},{"Oil",3},{"Glue",1}}},
        {"Super Smoothie",{{"Neonberry",3},{"Star Jelly",3},{"Purple Potion",3},{"Tropical Drink",6}}},
        {"Turpentine",{{"Super Smoothie",10},{"Caustic Wax",10},{"Star Jelly",100},{"Honeysuckle",1000}}}};
    if (!amount || amount>999) throw std::invalid_argument("Invalid crafting amount");
    auto cost=recipes.at(recipe); for (auto& [item,n]:cost) n*=amount; return cost;
}
}
