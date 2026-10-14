#pragma once
#include "core/runtime/model.hpp"
#include <map>
#include <mutex>

namespace hive::game {
using ItemAmounts=std::map<std::string,std::uint64_t>;
struct ItemCount { std::uint64_t count{}; Time updated{}; bool estimated{}; };
struct ItemStockView { ItemAmounts reserves; std::map<std::string,ItemCount> counts; };
class ItemStock {
    mutable std::mutex mutex_;
    ItemStockView state_;
public:
    void configure(ItemAmounts reserves);
    void observe(const std::string& item,std::uint64_t count,Time at);
    std::vector<std::string> unread(const ItemAmounts& cost) const;
    std::optional<std::string> blocked(const ItemAmounts& cost) const;
    bool spend(const ItemAmounts& cost,Time at);
    ItemStockView read() const;
};
ItemAmounts blender_cost(const std::string& recipe,unsigned amount);
}
