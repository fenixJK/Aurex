#pragma once
#include "core/session/session.hpp"
#include "core/module_host/module.hpp"
#include "game/detection/inventory.hpp"
#include "reporting/report.hpp"
#include <QStringList>
#include "game/detection/nectar.hpp"

namespace hive::app {
using DetectionSources=std::map<std::string,std::shared_ptr<const game::DetectionSource>>;
std::vector<game::DetectorConfig> game_detectors(std::shared_ptr<game::InventoryRequests> inventory_requests);
class ReportObservations {
    std::shared_ptr<const game::DetectionSource> reporting_nectar_,reporting_buffs_,reporting_upkeep_,reporting_honey_;
public:
    Observation<game::NectarLevels> nectar() const;
    void configure(const DetectionSources& sources);
    void request(const SessionSnapshot& snapshot,const QStringList& selected) const;
    reporting::Snapshot snapshot(const SessionSnapshot& snapshot,const game::ObservedState& readings,
        QString state,QString activity,const std::vector<extensions::ModuleReport>& modules) const;
};
}
