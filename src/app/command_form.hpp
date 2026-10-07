#pragma once
#include "hive/module_host/commands.hpp"
class QWidget;
namespace hive::app {
std::optional<extensions::CommandArguments> command_arguments(QWidget* parent,const extensions::Command& command);
void command_result(QWidget* parent,const extensions::CommandResult& result);
}
