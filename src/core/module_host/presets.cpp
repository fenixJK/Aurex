#include "host.hpp"
#include "configuration.hpp"
#include <QJsonArray>
#include <QStringList>
#include <algorithm>

namespace hive::extensions {
std::vector<QJsonObject> Host::starter_presets() const {
    struct Profile { QJsonObject modules; std::vector<std::pair<int,std::string>> order; QStringList details,enabled,disabled; };
    std::map<std::string,Profile> profiles;
    for (const auto& [id,entry]:entries_) if (entry.manifest.presets && entry.config_valid) {
        for (const auto& setup:entry.manifest.setup_profiles) {
            auto settings=entry.settings;
            for (const auto& [key,value]:setup.if_empty)
                if (std::get<SettingSelection>(settings.at(key)).empty()) settings[key]=value;
            for (const auto& [key,value]:setup.settings) settings[key]=value;
            validate_settings(entry.manifest,settings);
            auto& profile=profiles[setup.name];
            const bool enabled=setup.enabled.value_or(entry.requested);
            profile.modules[QString::fromStdString(id)]=QJsonObject{{"version",entry.manifest.settings_version},
                {"enabled",enabled},{"settings",encode_settings(settings)}};
            if (enabled) profile.order.emplace_back(setup.order,id);
            else profile.disabled.push_back(QString::fromStdString(entry.manifest.name));
            if (enabled && !entry.requested) profile.enabled.push_back(QString::fromStdString(entry.manifest.name));
            if (enabled && !setup.description.empty()) profile.details.push_back(QString::fromStdString(entry.manifest.name+": "+setup.description));
            for (const auto& setting:entry.manifest.settings) if (enabled && setup.description.empty() && setup.settings.contains(setting.id)) {
                const auto* value=std::get_if<bool>(&setup.settings.at(setting.id));
                if (value) profile.details.push_back(QString::fromStdString(entry.manifest.name+": "+setting.label+(*value?" on":" off")));
            }
        }
    }
    std::vector<QJsonObject> result;
    for (auto& [name,profile]:profiles) {
        std::sort(profile.order.begin(),profile.order.end());
        QJsonArray order; QStringList names;
        for (const auto& [priority,id]:profile.order) { order.append(QString::fromStdString(id)); names.push_back(QString::fromStdString(entries_.at(id).manifest.name)); }
        QStringList description{names.empty()?"No modules enabled":"Run order: "+names.join(" → ")};
        if (!profile.enabled.empty()) description.push_back("Turns on: "+profile.enabled.join(", "));
        if (!profile.disabled.empty()) description.push_back("Off: "+profile.disabled.join(", "));
        if (!profile.details.empty()) description.push_back(profile.details.join("\n"));
        result.push_back({{"format","aurex-preset"},{"version",1},{"name",QString::fromStdString(name)},
            {"modules",profile.modules},{"order",order},
            {"description",description.join("\n")}});
    }
    return result;
}
QJsonObject Host::preset() const {
    QJsonObject modules;
    for (const auto& [id,entry]:entries_) if (entry.manifest.presets && entry.config_valid)
        modules[QString::fromStdString(id)]=QJsonObject{{"version",entry.manifest.settings_version},
            {"enabled",entry.requested},{"settings",encode_settings(entry.settings)}};
    return {{"format","aurex-preset"},{"version",1},{"modules",modules}};
}
void Host::apply_preset(const QJsonObject& preset) {
    require_stopped();
    if (preset["format"]!="aurex-preset" || preset["version"]!=QJsonValue(1) || !preset["modules"].isObject())
        throw std::invalid_argument("Not an Aurex preset");
    Configuration proposed;
    const auto modules=preset["modules"].toObject();
    for (auto it=modules.begin();it!=modules.end();++it) {
        const auto id=it.key().toStdString();
        const auto found=entries_.find(id);
        if (found==entries_.end()) throw std::invalid_argument("Install the preset's module first: "+id);
        const auto& manifest=found->second.manifest;
        if (!manifest.presets) throw std::invalid_argument("This module keeps its settings locally: "+manifest.name);
        const auto item=it->toObject();
        const auto version=item["version"].toInt();
        if (!it->isObject() || version<1 || item["version"]!=QJsonValue(version)
            || !item["enabled"].isBool() || !item["settings"].isObject()) throw std::invalid_argument("Invalid module preset: "+manifest.name);
        auto values=decode_settings(item["settings"].toObject());
        if (version!=manifest.settings_version) {
            const auto builtin=builtins_.find(id);
            if (version>manifest.settings_version || builtin==builtins_.end() || !builtin->second.migration)
                throw std::invalid_argument("Preset needs a compatible version of "+manifest.name);
            values=builtin->second.migration(version,values);
        }
        auto settings=defaults(manifest);
        for (const auto& [key,value]:values) settings[key]=value;
        validate_settings(manifest,settings);
        proposed.emplace(id,ModuleConfiguration{std::move(settings),item["enabled"].toBool()});
    }
    const auto enabled=[&](const std::string& id) { return proposed.contains(id)?proposed.at(id).enabled:entries_.at(id).requested; };
    for (const auto& [id,entry]:entries_) if (enabled(id))
        for (const auto& dependency:order(id)) if (!enabled(dependency))
            throw std::invalid_argument(entry.manifest.name+" requires "+entries_.at(dependency).manifest.name);
    std::map<std::string,std::unique_ptr<Module>> pending;
    auto commands=commands_;
    try {
        for (const auto& [id,values]:proposed) {
            const auto& entry=entries_.at(id);
            if (entry.settings==values.settings && entry.requested==values.enabled && entry.config_valid
                && (!values.enabled || entry.active())) continue;
            commands.remove(id);
            auto& replacement=pending[id];
            if (!values.enabled) continue;
            replacement=factories_.at(entry.manifest.provider)(entry.manifest);
            if (!replacement) throw std::runtime_error("Provider returned an empty module");
            replacement->start(values.settings);
            if (!replacement->executable() && !entry.manifest.permissions.empty())
                throw std::runtime_error("Read-only modules cannot request input permissions");
            commands.replace(id,replacement->commands(),command_handler(id));
        }
        persist(&proposed);
    } catch (...) {
        for (auto& [id,instance]:pending) if (instance) instance->stop();
        throw;
    }
    for (auto& [id,instance]:pending) {
        auto& entry=entries_.at(id);
        stop_entry(entry);
        entry.instance_=std::move(instance); entry.revision_=next_revision_++;
        entry.settings=std::move(proposed.at(id).settings); entry.requested=proposed.at(id).enabled;
        entry.config_valid=true; entry.issue.clear();
    }
    commands_=std::move(commands);
}
}
