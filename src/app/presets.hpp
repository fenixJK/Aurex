#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>
#include <vector>

class QWidget;
namespace hive {
void show_presets(QWidget* parent,const QString& folder,std::function<QJsonObject()> capture,
    std::function<void(const QJsonObject&)> apply,std::function<bool()> busy,std::vector<QJsonObject> starters={});
}
