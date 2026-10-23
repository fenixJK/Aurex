#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace hive::scripting {
using BundledFiles=std::map<std::string,std::string>;
using LegacyFiles=std::map<std::string,std::set<std::uint64_t>>;
// Update only missing files or exact shipped defaults. User edits stay local.
std::vector<std::string> install_bundled_files(const std::filesystem::path& directory,
    const BundledFiles& bundled,const LegacyFiles& legacy={});
}
