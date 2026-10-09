#pragma once
#include "core/runtime/model.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <map>

namespace hive::game {
using WallTime=std::chrono::sys_time<Time>;
using WallClock=std::function<WallTime()>;
struct Cooldown {
    WallTime started,ready;
    bool estimated{};
    bool operator==(const Cooldown&) const=default;
};
using CooldownRecords=std::map<std::string,Cooldown>;
void validate(const CooldownRecords& records);
class CooldownStore {
public:
    virtual ~CooldownStore()=default;
    virtual CooldownRecords load() const=0;
    virtual void save(const CooldownRecords& records)=0;
};
class Cooldowns {
    std::shared_ptr<CooldownStore> store_;
    WallClock clock_;
    mutable std::mutex mutex_;
    CooldownRecords records_;
    bool loaded_{},dirty_{};
public:
    Cooldowns(std::shared_ptr<CooldownStore> store,WallClock clock);
    void load();
    void flush();
    CooldownRecords records() const;
    Time remaining(const std::string& id,Time cooldown) const;
    void remember(const std::string& id,Time started,Time ready,bool estimated,const ObservationContext& context);
};
}
