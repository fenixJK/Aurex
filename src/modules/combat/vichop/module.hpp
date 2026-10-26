#pragma once
#include "hop.hpp"
namespace hive::features::vichop {
extensions::Manifest module_manifest(const combat::Plan& plan);
extensions::Factory module_factory(Services services);
}
