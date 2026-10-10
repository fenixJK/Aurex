#include "configuration.hpp"
#include "module.hpp"
#include <QJsonArray>
#include <stdexcept>
#include <type_traits>

namespace hive::extensions {
QJsonObject encode_settings(const Settings& settings) {
    QJsonObject object;
    for (const auto& [key,value] : settings) std::visit([&,name=QString::fromStdString(key)](const auto& v) {
        using T=std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T,std::string>) object[name]=QString::fromStdString(v);
        else if constexpr (std::is_same_v<T,SettingSelection>) {
            QJsonArray items; for (const auto& item : v) items.append(QString::fromStdString(item)); object[name]=items;
        }
        else object[name]=v;
    },value);
    return object;
}
Settings decode_settings(const QJsonObject& object) {
    Settings result;
    for (auto it=object.begin(); it!=object.end(); ++it) {
        if (it->isBool()) result[it.key().toStdString()]=it->toBool();
        else if (it->isString()) result[it.key().toStdString()]=it->toString().toStdString();
        else if (it->isDouble() && it->toDouble()==it->toInt()) result[it.key().toStdString()]=it->toInt();
        else if (it->isArray() && it->toArray().size()<=static_cast<qsizetype>(maximum_setting_choices)) {
            SettingSelection items;
            for (const auto& item : it->toArray()) {
                if (!item.isString() || item.toString().size()>80) throw std::runtime_error("Invalid stored selection");
                items.push_back(item.toString().toStdString());
            }
            result[it.key().toStdString()]=std::move(items);
        }
        else throw std::runtime_error("Invalid stored setting type");
    }
    return result;
}
}
