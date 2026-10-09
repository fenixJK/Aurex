#pragma once
#include "module.hpp"
#include <filesystem>

namespace hive::features::gathering {
struct PatternFiles {
    std::vector<PatternPreset> presets;
    std::vector<std::string> errors;
};
PatternControls default_pattern_controls();
std::vector<std::string> install_default_patterns(const std::filesystem::path& directory);
PatternFiles read_pattern_files(const std::filesystem::path& directory);
}
