#pragma once
#include "tracker.hpp"
#include "cycle.hpp"
#include "hive/module_host/module.hpp"

namespace hive::features::quests {
class BrowserSource;
struct Snapshot {
    ObservationContext context;
    std::vector<Observation<Card>> cards;
};
class Source {
public:
    virtual ~Source()=default;
    virtual std::optional<Snapshot> read() const=0;
    virtual std::optional<Snapshot> peek() const { return read(); }
};
extensions::Manifest module_manifest(std::string id="hive.quests",std::string provider="native-quests",bool browser=false,bool cycle=false);
extensions::Factory module_factory(std::shared_ptr<const Source> source,std::vector<Field> fields,
    Time freshness=Time(30000),int priority=40,std::shared_ptr<const BrowserSource> browser={},Time retention=Time{},std::optional<CyclePlan> cycle={});
}
