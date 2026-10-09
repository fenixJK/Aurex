#pragma once
#include "core/runtime/program.hpp"
#include "core/module_host/contributor.hpp"
#include "core/module_host/commands.hpp"
#include <map>

namespace hive::scripting {
using Sources = std::map<std::string,std::string>;
std::unique_ptr<automation::Program> script_program(Sources sources,const Settings& settings,extensions::CommandArguments arguments={});
std::unique_ptr<automation::Program> script_pattern(Sources sources,const Settings& settings);
std::unique_ptr<automation::Contributor> script_contributor(Sources sources,const Settings& settings);
}
