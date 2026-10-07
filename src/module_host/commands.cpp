#include "hive/module_host/commands.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace hive::extensions {
namespace {
bool identifier(const std::string& value) {
    return !value.empty() && value.size()<=32 && value.front()>='a' && value.front()<='z'
        && std::all_of(value.begin(),value.end(),[](char c){return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='-';});
}
bool valid_value(const CommandArgument& field,const CommandValue& value) {
    if (field.type==ArgumentType::text) {
        const auto* text=std::get_if<std::string>(&value);
        return text && text->size()>=field.minimum && text->size()<=field.maximum
            && (field.choices.empty() || std::find(field.choices.begin(),field.choices.end(),*text)!=field.choices.end());
    }
    if (field.type==ArgumentType::boolean) return std::holds_alternative<bool>(value);
    const auto* integer=std::get_if<int>(&value);const auto* real=std::get_if<double>(&value);
    if (!integer && !(real && field.type==ArgumentType::number)) return false;
    const double number=integer?*integer:*real;
    return std::isfinite(number) && number>=field.minimum && number<=field.maximum;
}
}
void validate_commands(const std::vector<Command>& commands) {
    if (commands.size()>64) throw std::invalid_argument("Too many module commands");
    std::set<std::string> ids,aliases;
    for (const auto& command:commands) {
        if (!identifier(command.id) || !ids.insert(command.id).second || command.label.empty() || command.label.size()>100
            || command.description.size()>100 || (!command.alias.empty() && (!identifier(command.alias) || !aliases.insert(command.alias).second))
            || (command.access!=CommandAccess::read && command.access!=CommandAccess::stopped && command.access!=CommandAccess::queued)
            || (command.launches && (command.plot || command.access!=CommandAccess::stopped)) || command.arguments.size()>24)
            throw std::invalid_argument("Invalid module command declaration");
        std::set<std::string> arguments;
        for (const auto& argument:command.arguments) {
            if (!identifier(argument.id) || argument.id=="device" || !arguments.insert(argument.id).second || argument.label.empty()
                || argument.label.size()>100 || argument.description.size()>100 || !std::isfinite(argument.minimum) || !std::isfinite(argument.maximum)
                || argument.minimum>argument.maximum || argument.choices.size()>64
                || (argument.type!=ArgumentType::text && argument.type!=ArgumentType::integer && argument.type!=ArgumentType::number && argument.type!=ArgumentType::boolean)
                || (argument.type==ArgumentType::text && (argument.minimum<0 || argument.maximum>6000 || std::floor(argument.minimum)!=argument.minimum || std::floor(argument.maximum)!=argument.maximum))
                || (argument.type==ArgumentType::integer && (argument.minimum<std::numeric_limits<int>::min() || argument.maximum>std::numeric_limits<int>::max() || std::floor(argument.minimum)!=argument.minimum || std::floor(argument.maximum)!=argument.maximum))
                || (argument.type!=ArgumentType::text && !argument.choices.empty()))
                throw std::invalid_argument("Invalid command argument declaration");
            std::set<std::string> choices;
            for (const auto& choice:argument.choices)
                if (choice.empty() || choice.size()>100 || choice.size()<argument.minimum || choice.size()>argument.maximum || !choices.insert(choice).second)
                    throw std::invalid_argument("Invalid command choices");
            if (argument.initial && !valid_value(argument,*argument.initial)) throw std::invalid_argument("Invalid command argument default");
        }
    }
}
CommandInput validate_input(const Command& command,CommandInput input) {
    for (const auto& [id,value]:input.arguments) {
        const auto found=std::find_if(command.arguments.begin(),command.arguments.end(),[&](const auto& field){return field.id==id;});
        if (found==command.arguments.end() || !valid_value(*found,value)) throw std::invalid_argument("Invalid command argument: "+id);
    }
    for (const auto& argument:command.arguments) if (!input.arguments.contains(argument.id)) {
        if (argument.initial) input.arguments.emplace(argument.id,*argument.initial);
        else if (argument.required) throw std::invalid_argument("Missing command argument: "+argument.id);
    }
    for(const auto& argument:command.arguments)if(argument.type==ArgumentType::number) {
        if(const auto found=input.arguments.find(argument.id);found!=input.arguments.end())
            if(const auto* integer=std::get_if<int>(&found->second))found->second=double(*integer);
    }
    if (!command.target_prompt.empty() && (!input.target || !input.target->valid())) throw std::invalid_argument("Select a target before starting this tool");
    if (command.target_prompt.empty() && input.target) throw std::invalid_argument("This command does not accept a target");
    return input;
}
void CommandRegistry::replace(const std::string& owner,std::vector<Command> commands,Handler handler) {
    if (owner.empty() || !handler) throw std::invalid_argument("Commands need an owner and handler");
    validate_commands(commands);
    for (const auto& command:commands) if (!command.alias.empty())
        for (const auto& [other,provider]:providers_) if (other!=owner)
            for (const auto& existing:provider.commands) if (existing.alias==command.alias) throw std::invalid_argument("Command name already registered: "+command.alias);
    providers_.insert_or_assign(owner,Provider{std::move(commands),std::move(handler)});++revision_;
}
void CommandRegistry::remove(const std::string& owner) noexcept { if (providers_.erase(owner)) ++revision_; }
std::vector<RegisteredCommand> CommandRegistry::entries() const {
    std::vector<RegisteredCommand> result;
    for (const auto& [owner,provider]:providers_) for (const auto& command:provider.commands) result.push_back({owner,command});
    return result;
}
const Command& CommandRegistry::find(const std::string& owner,const std::string& id) const {
    if (const auto provider=providers_.find(owner);provider!=providers_.end())
        for (const auto& command:provider->second.commands) if (command.id==id) return command;
    throw std::invalid_argument("Command unavailable; enable its module first");
}
CommandResult CommandRegistry::invoke(const std::string& owner,const std::string& id,CommandInput input,bool busy,bool remote) const {
    const auto command=find(owner,id);
    if (remote && (!command.remote || !command.target_prompt.empty())) throw std::invalid_argument("This command is local only");
    if (busy && command.access==CommandAccess::stopped) throw std::logic_error("Stop the current session before running this command");
    input=validate_input(command,std::move(input));
    const auto handler=providers_.at(owner).handler;
    auto result=handler(id,input);if(result.title.empty())result.title=command.label;
    return result;
}
}
