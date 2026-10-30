#pragma once
#include "core/runtime/settings.hpp"
#include <span>

namespace hive::extensions {
using Value = SettingValue;
using Settings = hive::Settings;
inline constexpr std::size_t maximum_setting_choices=64;
struct ChoiceGroup { std::string label; std::vector<std::string> choices; };
struct Setting {
    std::string id, label;
    Value initial;
    int minimum{}, maximum{};
    std::vector<std::string> choices;
    bool ordered{};
    bool hidden{};
    bool inline_choices{};
    std::string help;
    std::string suffix;
    unsigned decimals{};
    std::vector<ChoiceGroup> groups;
    std::string enabled_by;
    int step{1};
    std::string step_toggle;
    int checked_step{1};
    std::map<std::string,std::string> icons;
    std::vector<std::string> enabled_values;
    std::map<std::string,std::string> dependent_labels;
    std::string minimum_label;
    // Inline selections cannot include the value chosen by this choice setting.
    std::string excluded_by;
};
void validate_setting(const Setting& setting,const Value& value);
void validate_settings(std::span<const Setting> schema,const Settings& settings);
}
