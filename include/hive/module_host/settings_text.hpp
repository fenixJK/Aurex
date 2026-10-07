#pragma once
#include "module.hpp"
#include <QString>
namespace hive::extensions {
QString setting_text(const Setting& field,const Value& value);
Value parse_setting(const Setting& field,const QString& text);
}
