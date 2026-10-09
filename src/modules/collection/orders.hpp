#pragma once
#include "game/detection/item_menu.hpp"
#include "game/data/cooldowns.hpp"
#include <filesystem>

namespace hive::features::collection {
struct ItemOrder {
    std::string item;
    unsigned amount{1},runs{1};
    bool unlimited{};
    bool operator==(const ItemOrder&) const=default;
};
struct OrderProgress {
    ItemOrder order;
    unsigned completed{};
    game::WallTime blocked_until{};
};
struct ItemQueue {
    std::vector<OrderProgress> orders;
    std::string last;
    std::optional<ItemOrder> crafting;
    game::WallTime ready{};
};
struct ItemOrderState { ItemQueue blender,shrine; };
class ItemOrderStore {
public:
    virtual ~ItemOrderStore()=default;
    virtual ItemOrderState load() const=0;
    virtual void save(const ItemOrderState&)=0;
};
class ItemOrders {
    std::shared_ptr<ItemOrderStore> store_;
    game::WallClock clock_;
    mutable std::mutex mutex_;
    mutable ItemOrderState state_;
    mutable bool loaded_{};
    void load() const;
    ItemQueue& queue(game::ItemMenuKind);
    void save(const ItemOrderState&);
public:
    explicit ItemOrders(std::shared_ptr<ItemOrderStore> store={},game::WallClock clock={});
    void configure(game::ItemMenuKind,const std::vector<ItemOrder>&);
    std::optional<ItemOrder> next(game::ItemMenuKind) const;
    bool pending(game::ItemMenuKind) const;
    void accepted(game::ItemMenuKind,const ItemOrder&,Time delay);
    void collected();
    void defer(game::ItemMenuKind,Time delay);
    void missing(game::ItemMenuKind,const std::string& item);
    void reset(game::ItemMenuKind);
    ItemOrderState read() const;
};
void validate(const ItemOrderState&);
std::shared_ptr<ItemOrderStore> item_order_file_store(const std::filesystem::path&);
}
