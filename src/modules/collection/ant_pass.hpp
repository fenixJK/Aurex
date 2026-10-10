#pragma once
#include "core/runtime/program.hpp"
#include <mutex>

namespace hive::features::collection {
class AntPassBudget {
    mutable std::mutex mutex_;
    std::uint64_t session_{};
    unsigned limit_{},spent_{};
public:
    void configure(unsigned limit,std::uint64_t session);
    void disable();
    bool available() const;
    bool reserve();
};
automation::ProgramFactory ant_pass_purchase(std::shared_ptr<AntPassBudget> budget,
    automation::ProgramFactory prepare,automation::ProgramFactory home,
    std::shared_ptr<const game::InventorySource> inventory,double speed);
}
