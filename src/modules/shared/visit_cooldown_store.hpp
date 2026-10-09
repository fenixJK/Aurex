#pragma once
#include "visit_cooldowns.hpp"
#include <filesystem>

namespace hive::features::visits {
std::shared_ptr<CooldownStore> cooldown_file_store(std::filesystem::path file);
}
