#pragma once
#include "reporting/remote.hpp"
namespace hive { class ExtensionsUi; }
namespace hive::app {
QList<reporting::RemoteCommand> module_remote_commands(ExtensionsUi& modules);
}
