#pragma once
#include "plan.hpp"
#include "game/data/cooldowns.hpp"
#include "core/module_host/module.hpp"

namespace hive::features::combat {
// Feed observed kills into the same timers used by scheduled combat visits.
class DefeatCooldowns {
    struct Target {
        std::string id,species,field;
        unsigned count{};
        Time cooldown{};
        bool reduced{},unique{};
        std::vector<Time> kills;
    };
    struct Source {
        std::shared_ptr<const game::DetectionSource> observations;
        std::vector<Target> targets;
        std::uint64_t last_event{};
    };
    std::vector<Source> sources_;
    std::shared_ptr<game::Cooldowns> saved_;
    std::uint64_t generation_{},session_{};
    Time field_since_{};
    std::string field_;
    int reduction_{};
public:
    DefeatCooldowns(const Plan&,std::shared_ptr<game::Cooldowns>);
    void configure(int reduction);
    void update(const extensions::SessionView&);
};
}
