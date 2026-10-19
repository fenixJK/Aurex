#include "observations.hpp"
#include "game/detection/buffs.hpp"
#include "game/detection/field_boosts.hpp"
#include "game/detection/movement_buffs.hpp"
#include "game/detection/honey.hpp"
#include "game/detection/nectar.hpp"
#include "game/detection/memory_match.hpp"
#include "game/detection/daylight.hpp"
#include "game/detection/item_menu.hpp"
#include "game/detection/hive_view.hpp"
#include "game/detection/sprinkler_reference.hpp"
#include "game/detection/menus.hpp"
#include "game/actions/travel/localization.hpp"
#include "game/actions/hive/hive_setup.hpp"
#include "game/detection/planters.hpp"
#include "game/detection/blooms.hpp"
#include "game/detection/conversation.hpp"
#include "game/detection/enemies.hpp"
#include <QDir>
#include <algorithm>

namespace hive::app {
namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
struct BuffStyle { QImage icon; QColor color; QString chart_group; };
BuffStyle buff_style(QString name) {
    static const QMap<QString,BuffStyle> styles=[] {
        QMap<QString,BuffStyle> result{
            {"blue-boost",{{},"#73aaf2","pollen-boost"}}, {"red-boost",{{},"#ee7773","pollen-boost"}},
            {"white-boost",{{},"#e9e9e9","pollen-boost"}}, {"pop-star",{{},"#94d9ef"}},
            {"melody",{{},"#e8e8e8"}}, {"jelly-bean-sharing",{{},"#f9ccff"}},
            {"guiding-star",{{},"#ead582"}}, {"reindeer-fetch",{{},"#cc4848"}},
            {"festive-mark",{{},"#7ab55e"}},
            {"focus",{{},"#6cde83"}}, {"bomb-combo",{{},"#e1e5ea"}},
            {"balloon-aura",{{},"#f0d66c"}}, {"haste",{{},"#bdc8d3"}},
            {"precision",{{},"#ba83e1"}}, {"inspire",{{},"#f3e3a1"}},
            {"honey-mark",{{},"#edbb55"}}, {"pollen-mark",{{},"#e7d86f"}},
            {"rage",{{},"#efa451"}}, {"baby-love",{{},"#eea6c2"}},
            {"haste+",{{},"#dfd26c"}}, {"oil",{{},"#e7c47e"}},
            {"smoothie",{{},"#cba1e8"}}, {"bear-morph",{{},"#cfaa83"}},
            {"comforting",{{},"#7e9eb3"}}, {"refreshing",{{},"#78b375"}},
            {"satisfying",{{},"#b398a7"}}, {"motivating",{{},"#937db3"}},
            {"invigorating",{{},"#b35951"}}
        };
        const QDir directory(":/buffs");
        for (const auto& file:directory.entryList({"*.png"},QDir::Files))
            result[file.chopped(4)].icon=QImage(directory.filePath(file)).scaled(36,36,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        return result;
    }();
    return styles.value(name.toLower().replace(' ','-'));
}
hive::reporting::Metric report_metric(const hive::extensions::Metric& metric) {
    return {q(metric.label),q(metric.value),metric.progress,buff_style(q(metric.color_key)).color};
}
}
std::vector<game::DetectorConfig> game_detectors(std::shared_ptr<game::InventoryRequests> inventory_requests) {
    std::vector<hive::game::DetectorConfig> detectors{
        {"honey",hive::game::honey_detector(),hive::Time(hive::reporting::sample_interval_ms)},
        {"enemies",hive::game::enemy_detector()},
        {"memory-board",hive::game::memory_match_detector(),hive::Time(250),hive::game::DetectionMode::pixels},
        {"daylight",hive::game::daylight_detector(),hive::Time(2000),hive::game::DetectionMode::pixels,true},
        {"item-menu",hive::game::item_menu_detector(),hive::Time(750),hive::game::DetectionMode::text,true},
        {"buff-upkeep",hive::game::buff_detector(),hive::Time(6000),hive::game::DetectionMode::pixels},
        {"field-boosts",hive::game::field_boost_detector(),hive::Time(1000),hive::game::DetectionMode::pixels},
        {"movement-buffs",hive::game::movement_buff_detector(),hive::Time(100),hive::game::DetectionMode::pixels,true,hive::game::DetectionPriority::responsive},
        {"respawn-menu",hive::game::respawn_detector(),hive::Time(100),hive::game::DetectionMode::pixels},
        {"nectar",hive::game::nectar_detector(),hive::Time(750),hive::game::DetectionMode::pixels},
        {"planters",hive::game::planter_detector()},
        {"inventory",hive::game::inventory_detector(hive::game::inventory_layout(),inventory_requests)},
        {"blooms",hive::game::bloom_detector(),hive::Time(250),hive::game::DetectionMode::pixels},
        {"npc-conversation",hive::game::conversation_detector(),hive::Time(100),hive::game::DetectionMode::pixels},
        {"navigation-landmarks",hive::game::landmark_detector(hive::game::interaction_anchors()),hive::Time(750)},
        {"interaction-prompt",hive::game::interaction_prompt_detector(),hive::Time(100),hive::game::DetectionMode::pixels},
        {"planter-harvest",hive::game::interaction_prompt_detector("Harvest"),hive::Time(100),hive::game::DetectionMode::pixels},
        {"sprinkler-reference",hive::game::sprinkler_reference_detector(),hive::Time(500),hive::game::DetectionMode::pixels},
        {"spawn-view",hive::game::spawn_view_detector(),hive::Time(250),hive::game::DetectionMode::pixels},
        {"hive-view",hive::game::hive_view_detector(),hive::Time(125),hive::game::DetectionMode::pixels},
        {"hive-slots",hive::game::hive_slots_detector(),hive::Time(250),hive::game::DetectionMode::pixels}};
    for (auto& detector:detectors) detector.on_demand=true;
    return detectors;
}
Observation<game::NectarLevels> ReportObservations::nectar() const {
    if (reporting_nectar_) if (const auto packet=reporting_nectar_->peek())
        if (const auto* detection=dynamic_cast<const game::NectarDetection*>(packet->value.get())) return detection->levels;
    return {};
}
void ReportObservations::configure(const DetectionSources& sources) {
    reporting_honey_=sources.at("honey"); reporting_nectar_=sources.at("nectar");
    reporting_buffs_=sources.at("movement-buffs"); reporting_upkeep_=sources.at("buff-upkeep");
}
void ReportObservations::request(const SessionSnapshot& snapshot,const QStringList& selected) const {
    if (snapshot.status.state==hive::RunState::running && reporting_nectar_) {
        if (reporting_honey_) {
            const auto reading=reporting_honey_->peek();
            if (!reading || reading->context.generation!=snapshot.generation || snapshot.now-reading->context.now>=hive::Time(hive::reporting::sample_interval_ms)) reporting_honey_->read();
        }
        for (const auto& source:{reporting_buffs_,reporting_upkeep_}) if (source) {
            const bool needed=source==reporting_buffs_?std::any_of(selected.begin(),selected.end(),[](const auto& id) {
                    return id=="buff/Haste" || id=="buff/Haste+" || id=="buff/Oil" || id=="buff/Smoothie" || id=="buff/Bear morph";
                }):
                std::any_of(hive::game::tracked_buffs().begin(),hive::game::tracked_buffs().end(),[&](const auto& type) {
                    return selected.contains("buff/"+QString::fromUtf8(type.name.data(),qsizetype(type.name.size())));
                });
            if (!needed) continue;
            const auto reading=source->peek();
            if (!reading || reading->context.generation!=snapshot.generation || snapshot.now-reading->context.now>hive::Time(source==reporting_buffs_?3000:6000)) source->read();
        }
        const auto nectar=reporting_nectar_->peek();
        if (!nectar || nectar->context.generation!=snapshot.generation || snapshot.now-nectar->context.now>hive::Time(3000)) reporting_nectar_->read();
    }
}
reporting::Snapshot ReportObservations::snapshot(const SessionSnapshot& snapshot,const game::ObservedState& readings,QString state,QString activity,const std::vector<extensions::ModuleReport>& modules) const {
    hive::reporting::Snapshot report{std::move(state),std::move(activity),{}};
    std::map<std::string,double> thresholds;
    for (const auto& module:modules) {
        hive::reporting::Section section{q(module.name),{},q(module.id),module.counters};
        for (const auto& metric:module.metrics) section.metrics.append(report_metric(metric));
        for (const auto& threshold:module.thresholds) thresholds[threshold.id]=threshold.minimum;
        report.sections.push_back(std::move(section));
    }
    report.counters={{"disconnects","Disconnects",snapshot.recovery.disconnects,snapshot.session_id},
        {"rejoins","Successful rejoins",snapshot.recovery.rejoins,snapshot.session_id}};
    const hive::ObservationContext context{snapshot.window,snapshot.generation,snapshot.now};
    report.trend.name="Backpack";
    if (reporting_honey_) if (const auto packet=reporting_honey_->peek()) {
        const auto* value=dynamic_cast<const hive::game::HoneyDetection*>(packet->value.get());
        if (value && value->amount.has_value(context,hive::Time(2*hive::reporting::sample_interval_ms))) {
            report.honey=value->amount.value;
            report.honey_age_ms=(context.now-value->amount.captured).count();
        }
    }
    if (readings.state.backpack_percent.has_value(context,hive::Time(5000))) report.trend.percent=readings.state.backpack_percent.value;
    for (const auto* name:{"Comforting","Refreshing","Satisfying","Motivating","Invigorating"}) {
        const auto id=std::string("nectar/")+name; const auto found=thresholds.find(id);
        report.gauges.append(hive::reporting::Gauge{name,{},q(id),found==thresholds.end()?std::nullopt:std::optional<double>(found->second)});
        report.gauges.back().color=buff_style(name).color;
    }
    if (reporting_nectar_) if (const auto result=reporting_nectar_->peek()) {
        const auto* nectar=dynamic_cast<const hive::game::NectarDetection*>(result->value.get());
        if (nectar && nectar->levels.has_value(context,hive::Time(5000)))
            for (int i=0;i<5;++i) report.gauges[i].percent=nectar->levels.value.percent[i];
    }
    for (const auto& type:hive::game::tracked_buffs()) {
        const auto name=QString::fromUtf8(type.name.data(),qsizetype(type.name.size()));
        report.buffs.append(hive::reporting::Gauge{name,{},"buff/"+name,{},hive::reporting::Level{{},double(type.maximum)}});
    }
    if (reporting_upkeep_) {
        if (const auto packet=reporting_upkeep_->peek()) {
            const auto* value=dynamic_cast<const hive::game::BuffDetection*>(packet->value.get());
            const auto& stamp=packet->context;
            const hive::Observation<bool> reading{hive::Knowledge::present,true,stamp.now,stamp.window,stamp.generation};
            if (value && reading.has_value(context,hive::Time(10000)))
                for (const auto& observed:value->buffs) for (auto& buff:report.buffs)
                    if (q(observed.name)==buff.name && observed.active) {
                        buff.percent=*observed.active?100:0;
                        if (buff.level) buff.level->value=observed.stacks;
                    }
        }
    }
    const auto movement_start=report.buffs.size();
    for (const auto* name:{"Haste","Haste+","Oil","Smoothie","Bear morph"})
        report.buffs.append(hive::reporting::Gauge{name,{},"buff/"+QString(name),{},hive::reporting::Level{{},QString(name)=="Haste"?10.:1.}});
    for (auto& buff:report.buffs) {
        const auto style=buff_style(buff.name); buff.icon=style.icon; buff.color=style.color; buff.chart_group=style.chart_group;
    }
    if (reporting_buffs_) if (const auto packet=reporting_buffs_->peek()) {
        const auto* value=dynamic_cast<const hive::game::MovementBuffDetection*>(packet->value.get());
        const auto& stamp=packet->context;
        const hive::Observation<bool> reading{hive::Knowledge::present,true,stamp.now,stamp.window,stamp.generation};
        if (value && value->buffs.layout_known && reading.has_value(context,hive::Time(5000))) {
            for (auto i=movement_start;i<report.buffs.size();++i) { report.buffs[i].percent=0; report.buffs[i].level->value=0; }
            for (const auto& buff:value->buffs.candidates) {
                const int index=buff.name=="Haste"?0:buff.name=="HastePlus"?1:buff.name=="Oil"?2:buff.name=="Smoothie"?3:buff.name.starts_with("Bear")?4:-1;
                if (index>=0) { report.buffs[movement_start+index].percent=100; report.buffs[movement_start+index].level->value=1; }
                if (index==0) report.buffs[movement_start].level->value=buff.stacks;
            }
        }
    }
    return report;
}
}
