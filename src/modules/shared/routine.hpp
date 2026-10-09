#pragma once
#include "core/runtime/program.hpp"
#include "core/module_host/activity_policy.hpp"

namespace hive::features::routine {
struct Activity {
    std::string id;
    int priority{};
    Time cooldown{60000}, boost_duration{};
    std::size_t confirmation{};
    bool during_boost{};
    std::vector<automation::Step> steps;
};
struct Routine {
    automation::RunConfig config;
    automation::AwaitText anchor;
    std::vector<Activity> activities;
    std::vector<automation::Step> gather;
};
std::unique_ptr<automation::Program> routine_program(Routine routine);
std::unique_ptr<automation::Program> routine_program(Routine routine,std::unique_ptr<automation::ActivityPolicy> policy);
}
