#pragma once
#include "controller.hpp"
#include "hive/module_host/module.hpp"

namespace hive::features::accounts {
extensions::Manifest module_manifest();
extensions::Factory module_factory(std::shared_ptr<Controller> controller);
}
