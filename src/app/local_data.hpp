#pragma once
#include <QSettings>
#include <QLockFile>
#include <memory>

namespace hive::app {
QSettings::Format json_settings_format();
QString portable_data_path(const QString& executable_directory);
void prepare_local_data(const QString& directory);
QString prepare_local_data();
std::unique_ptr<QLockFile> lock_local_data(const QString& directory);
}
