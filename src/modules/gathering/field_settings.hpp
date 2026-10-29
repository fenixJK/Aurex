#pragma once
#include "core/runtime/settings.hpp"

namespace hive::features::gathering {
std::string field_prefix(const std::string& field);
Settings import_natro_field(const std::string& field,const Settings& copied);
}
