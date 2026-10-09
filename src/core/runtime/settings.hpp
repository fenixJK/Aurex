#pragma once
#include <map>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace hive {
using SettingSelection = std::vector<std::string>;
using SettingValue = std::variant<bool,int,std::string,SettingSelection>;
using Settings = std::map<std::string,SettingValue>;
inline constexpr std::size_t maximum_settings=512;
}
