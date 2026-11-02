#include "collection_settings.hpp"
#include "configuration.hpp"
#include <QJsonDocument>

namespace hive::extensions {
namespace {
std::set<std::string> local_keys(const Manifest& manifest,const std::string& collection,const std::string& choice,const std::string& prefix) {
    std::set<std::string> keys,shared;
    if (manifest.page) for (const auto& section:manifest.page->sections) if (section.collection==collection)
        for (const auto& key:section.settings) if (key.starts_with(prefix) && key.size()>prefix.size())
            (section.choice==choice?keys:shared).insert(key);
    for (const auto& key:shared) keys.erase(key);
    if (keys.empty()) throw std::invalid_argument("Select a field first.");
    return keys;
}
}
QByteArray copy_collection_settings(const Manifest& manifest,const Settings& values,const std::string& collection,const std::string& choice) {
    const auto prefix=manifest.collection_prefix?manifest.collection_prefix(collection,choice):std::string{};
    Settings selected;
    for (const auto& key:local_keys(manifest,collection,choice,prefix)) selected[key.substr(prefix.size())]=values.at(key);
    return QJsonDocument(QJsonObject{{"format","Aurex field settings"},{"version",prefix.empty()?1:2},
        {"module",QString::fromStdString(manifest.id)},{"collection",QString::fromStdString(collection)},
        {"name",QString::fromStdString(choice)},{"settings",encode_settings(selected)}}).toJson(QJsonDocument::Compact);
}
Settings paste_collection_settings(const Manifest& manifest,const Settings& values,const std::string& collection,const std::string& choice,const QByteArray& data) {
    const auto prefix=manifest.collection_prefix?manifest.collection_prefix(collection,choice):std::string{};
    const auto keys=local_keys(manifest,collection,choice,prefix);
    if (data.size()>65536) throw std::invalid_argument("The copied settings are too large.");
    QJsonParseError error; const auto doc=QJsonDocument::fromJson(data,&error);
    if (error.error!=QJsonParseError::NoError || !doc.isObject()) throw std::invalid_argument("Copy a field settings JSON first.");
    const auto object=doc.object(); Settings patch; bool compact=false;
    if (object.contains("format")) {
        compact=object["version"]==2;
        if (object["format"]!="Aurex field settings" || (object["version"]!=1 && !compact) || (compact && prefix.empty())
            || object["module"]!=QString::fromStdString(manifest.id) || object["collection"]!=QString::fromStdString(collection)
            || !object["settings"].isObject()) throw std::invalid_argument("These settings are not compatible with this page.");
        if (object["name"]!=QString::fromStdString(choice)) throw std::invalid_argument("Select the field named in the copied settings first.");
        patch=decode_settings(object["settings"].toObject());
    } else if (manifest.import_collection) patch=manifest.import_collection(collection,choice,decode_settings(object));
    else throw std::invalid_argument("Copy field settings from Aurex first.");
    if (patch.empty()) throw std::invalid_argument("No field settings were found.");
    auto result=values;
    for (const auto& [key,value]:patch) {
        const auto resolved=compact?prefix+key:key;
        if (!keys.contains(resolved)) throw std::invalid_argument("The copied settings contain an unsupported field option: "+key);
        result[resolved]=value;
    }
    validate_settings(manifest,result);
    return result;
}
}
