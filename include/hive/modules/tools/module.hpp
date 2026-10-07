#pragma once
#include "hive/module_host/module.hpp"

namespace hive::features::tools {
extensions::Manifest module_manifest();
extensions::Factory module_factory(std::shared_ptr<const game::InventorySource> inventory={});
}
