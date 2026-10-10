#pragma once
#include "core/runtime/settings.hpp"
#include <QJsonObject>

namespace hive::extensions {
QJsonObject encode_settings(const Settings& settings);
Settings decode_settings(const QJsonObject& object);
}
