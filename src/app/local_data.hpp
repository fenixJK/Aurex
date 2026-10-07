#pragma once
#include <QSettings>

namespace hive::app {
QSettings::Format json_settings_format();
QString portable_data_path(const QString& executable_directory);
void prepare_local_data(const QString& directory);
QString prepare_local_data();
}
