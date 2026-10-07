#include "hive/module_host/providers.hpp"
#include "builtins.hpp"
#include "hive/module_host/host.hpp"

namespace hive::extensions {
void register_builtin_modules(Host& host) {
    host.register_provider("dashboard",dashboard_factory(),{},true);
    host.register_provider("script",script_factory(),{},true);
    host.register_provider("script-contributor",script_contributor_factory(),{{"fields",game::RequestLimits{}},{"enemies",game::RequestLimits{}}},true);
}
}
