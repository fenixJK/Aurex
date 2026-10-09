#pragma once
#include "controller.hpp"
#include <filesystem>

namespace hive::features::planters {
std::shared_ptr<Store> file_store(std::filesystem::path file);
}
