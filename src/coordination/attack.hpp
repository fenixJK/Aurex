#pragma once
#include "core/runtime/program.hpp"
#include <atomic>
#include <map>

namespace hive::coordination {
struct AttackAssistState {
    std::atomic_bool allowed{true},main_ready{},arrived{},finished{},defeated{};
};
struct AttackJob {
    std::string id,enemy;
    Time starts{},ends{};
    bool scheduled{};
};
struct AttackAssist {
    // Destinations come from the combat catalog, after route/access validation.
    std::map<std::string,std::string> destinations;
    std::function<std::unique_ptr<automation::Program>(const AttackJob&,bool,unsigned,std::shared_ptr<AttackAssistState>)> create;
    explicit operator bool() const { return bool(create); }
};
}
