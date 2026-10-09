#pragma once
#include "core/runtime/model.hpp"
#include <functional>
#include <map>
#include <memory>
#include <variant>

namespace hive::extensions {
enum class CommandAccess { read, stopped, queued };
enum class ArgumentType { text, integer, number, boolean };
using CommandValue=std::variant<std::string,int,double,bool>;
using CommandArguments=std::map<std::string,CommandValue>;
struct CommandArgument {
    std::string id,label,description;
    ArgumentType type{ArgumentType::text};
    bool required{};
    std::optional<CommandValue> initial;
    double minimum{},maximum{1000};
    std::vector<std::string> choices;
};
struct Command {
    std::string id,label,tool;
    bool plot{true},launches{};
    std::string tab,target_prompt;
    bool remote{};
    std::string description,alias;
    std::vector<CommandArgument> arguments;
    CommandAccess access{CommandAccess::stopped};
    bool visible{true};
    std::string script;
};
struct ModuleTimer { std::string id,label,detail; std::optional<Time> remaining; std::optional<double> progress; };
struct CommandInput { std::optional<WindowPoint> target; CommandArguments arguments; };
struct Plot;
struct CommandResult {
    std::string title,message;
    std::vector<std::pair<std::string,std::string>> fields;
    std::vector<ModuleTimer> timers;
    std::optional<double> progress;
    std::shared_ptr<const Frame> image;
    std::shared_ptr<const Plot> chart;
    std::function<CommandResult()> deferred;
};
struct RegisteredCommand { std::string owner; Command command; };
void validate_commands(const std::vector<Command>& commands);
CommandInput validate_input(const Command& command,CommandInput input);
class CommandRegistry {
public:
    using Handler=std::function<CommandResult(const std::string&,const CommandInput&)>;
    void replace(const std::string& owner,std::vector<Command> commands,Handler handler);
    void remove(const std::string& owner) noexcept;
    std::vector<RegisteredCommand> entries() const;
    const Command& find(const std::string& owner,const std::string& id) const;
    CommandResult invoke(const std::string& owner,const std::string& id,CommandInput input,bool busy,bool remote=false) const;
    std::uint64_t revision() const { return revision_; }
private:
    struct Provider { std::vector<Command> commands; Handler handler; };
    std::map<std::string,Provider> providers_;
    std::uint64_t revision_{};
};
}
