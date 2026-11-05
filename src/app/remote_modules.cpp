#include "remote_modules.hpp"
#include "extensions_ui.hpp"
#include "module_plot.hpp"
#include "core/module_host/settings_text.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace hive::app {
namespace {
using namespace reporting;
QString q(const std::string& value) { return QString::fromStdString(value); }
QString timer_value(const extensions::ModuleTimer& timer) {
    QString text=timer.remaining?*timer.remaining>Time{}?"Ready in "+duration(timer.remaining->count()):QString("Ready"):QString("Not confirmed");
    if(timer.progress) text=QString::number(*timer.progress,'f',0)+"% · "+text;
    if(!timer.detail.empty()) text=q(timer.detail)+"\n"+text;
    return text;
}
extensions::CommandInput command_input(const RemoteArguments& args) {
    extensions::CommandInput result;
    for(auto it=args.begin();it!=args.end();++it) {
        extensions::CommandValue value;
        if(it->isBool())value=it->toBool();
        else if(it->isString())value=it->toString().toStdString();
        else if(it->isDouble()) {
            const auto number=it->toDouble();
            if(!std::isfinite(number))throw std::invalid_argument("Command numbers must be finite");
            if(std::floor(number)==number && number>=std::numeric_limits<int>::min() && number<=std::numeric_limits<int>::max())value=int(number);
            else value=number;
        } else throw std::invalid_argument("Command arguments must be text, numbers or switches");
        result.arguments.emplace(it.key().toStdString(),std::move(value));
    }
    return result;
}
RemoteReply command_reply(const extensions::CommandResult& result) {
    RemoteReply reply{q(result.title),q(result.message)};
    if (result.deferred) {
        reply.deferred=[work=result.deferred] { return command_reply(work()); };
        return reply;
    }
    for(const auto& [label,value]:result.fields)reply.fields.append({q(label),q(value)});
    QList<RemoteCard> cards;
    for(const auto& timer:result.timers) cards.append({q(timer.label),timer_value(timer),timer.progress});
    reply.image=remote_cards(reply.title,cards);
    if(result.progress)reply.fields.append({"Progress",QString::number(*result.progress,'f',0)+"%"});
    if(result.image && result.image->valid()) {
        const auto& frame=*result.image;reply.image=QImage(frame.rgba.data(),frame.width,frame.height,frame.width*4,QImage::Format_RGBA8888).copy();
    }
    if (result.chart) reply.image=plot_image(*result.chart);
    return reply;
}

const extensions::Entry& module(const ExtensionsUi& modules,const RemoteArguments& args) {
    const auto name=args["module"].toString().trimmed();
    if (const auto found=modules.entries().find(name.toStdString());found!=modules.entries().end()) return found->second;
    const extensions::Entry* match=nullptr;
    for (const auto& [id,entry]:modules.entries()) if (q(entry.manifest.name).compare(name,Qt::CaseInsensitive)==0) {
        if (match) throw std::invalid_argument("Use the module ID to distinguish modules with the same name");
        match=&entry;
    }
    if (!match) throw std::invalid_argument("Choose an enabled feature from the module list");
    return *match;
}
RemoteChoices complete(const ExtensionsUi& modules,const QString&,const RemoteArguments& args) {
    RemoteChoices result;const auto term=args["module"].toString();
    for(const auto& [id,entry]:modules.entries())
        if(entry.active() && q(entry.manifest.name).contains(term,Qt::CaseInsensitive)) result.append({q(entry.manifest.name),q(id)});
    return result;
}
RemoteOption module_option() { return {"module","Feature to inspect; leave blank for all enabled features",RemoteOptionType::text,false,{},1,100,true}; }
bool manageable(const extensions::Entry& entry) {
    // Native features declare scheduling on their instance, not necessarily in
    // the manifest. The settings schema is the remote configuration contract.
    return !entry.manifest.settings.empty();
}
const extensions::Entry& managed_module(const ExtensionsUi& modules,const RemoteArguments& args) {
    const auto& entry=module(modules,args);
    if (!manageable(entry)) throw std::invalid_argument("This feature is configured in the app only");
    return entry;
}
std::vector<const extensions::Setting*> scoped_settings(const extensions::Entry& entry,const QString& item) {
    std::map<std::string,std::set<std::string>> scopes;
    bool found=item.isEmpty();
    if (entry.manifest.page) for (const auto& section:entry.manifest.page->sections) if (!section.choice.empty()) {
        if (q(section.choice).compare(item,Qt::CaseInsensitive)==0) found=true;
        for (const auto& key:section.settings) scopes[key].insert(section.choice);
    }
    if (!found) throw std::invalid_argument("Choose a field or item from the item list");
    std::vector<const extensions::Setting*> result;
    for (const auto& field:entry.manifest.settings) {
        if (field.hidden) continue;
        const auto scope=scopes.find(field.id);
        if (item.isEmpty()) {
            if (scope!=scopes.end() && scope->second.size()==1) continue;
        } else if (scope==scopes.end() || std::none_of(scope->second.begin(),scope->second.end(),[&](const auto& name) {
            return q(name).compare(item,Qt::CaseInsensitive)==0;
        })) continue;
        result.push_back(&field);
    }
    return result;
}
const extensions::Setting& setting(const extensions::Entry& entry,const RemoteArguments& args) {
    const auto fields=scoped_settings(entry,args["item"].toString());
    const auto key=args["setting"].toString().trimmed();
    for (const auto* field:fields) if (q(field->id)==key) return *field;
    const extensions::Setting* match=nullptr;
    for (const auto* field:fields) if (q(field->label).compare(key,Qt::CaseInsensitive)==0) {
        if (match) throw std::invalid_argument("Choose a setting from autocomplete to distinguish matching names");
        match=field;
    }
    if (!match) throw std::invalid_argument("Choose an item first for field settings, then choose its setting");
    return *match;
}
RemoteChoices setup_choices(const ExtensionsUi& modules,const QString& focused,const RemoteArguments& args) {
    RemoteChoices choices;
    const auto term=args[focused].toString();
    if (focused=="module") {
        for (const auto& [id,entry]:modules.entries())
            if (manageable(entry) && q(entry.manifest.name).contains(term,Qt::CaseInsensitive)) choices.append({q(entry.manifest.name),q(id)});
    } else if (focused=="item") {
        const auto& entry=managed_module(modules,args); std::set<std::string> seen;
        if (entry.manifest.page) for (const auto& section:entry.manifest.page->sections)
            if (!section.choice.empty() && seen.insert(section.choice).second && q(section.choice).contains(term,Qt::CaseInsensitive))
                choices.append({q(section.choice),q(section.choice)});
    } else if (focused=="setting") {
        for (const auto* field:scoped_settings(managed_module(modules,args),args["item"].toString()))
            if (q(field->label).contains(term,Qt::CaseInsensitive)) choices.append({q(field->label),q(field->id)});
    } else if (focused=="value") {
        const auto& field=setting(managed_module(modules,args),args);
        auto values=field.choices;
        if (std::holds_alternative<bool>(field.initial)) values={"on","off"};
        for (const auto& value:values) if (q(value).contains(term,Qt::CaseInsensitive)) choices.append({q(value),q(value)});
    }
    return choices;
}
RemoteOption page_option() { return {"page","Page number",RemoteOptionType::integer,false,{},1,1000}; }
RemoteReply detail(const extensions::Entry& entry) {
    RemoteReply result{q(entry.manifest.name),entry.requested?entry.active()?"Enabled":"Needs attention":"Disabled"};
    if (!entry.issue.empty()) result.description+="\n"+q(entry.issue);
    const auto report=entry.report();
    for (const auto& metric:report.metrics.empty()?entry.display:report.metrics)
        if (!metric.value.empty()) result.fields.append({q(metric.label),q(metric.value)});
    return result;
}
void paginate(RemoteReply& result,const RemoteArguments& args,int count=10) {
    const auto pages=std::max(qsizetype(1),(result.fields.size()+count-1)/count);
    const auto page=args["page"].toInt(1);
    if (page<1 || page>pages) throw std::invalid_argument("Page exceeds the available results");
    result.fields=result.fields.mid((page-1)*count,count);
    if (pages>1) result.description+="\nPage "+QString::number(page)+" / "+QString::number(pages);
}
}
QList<reporting::RemoteCommand> module_remote_commands(ExtensionsUi& modules) {
    using namespace reporting;
    const auto autocomplete=[&modules](const QString& key,const RemoteArguments& args) { return complete(modules,key,args); };
    QList<RemoteCommand> commands{
        {"progress","Show progress for your enabled features",false,[&modules](const RemoteArguments& args) {
            if(!args["module"].toString().isEmpty()) {
                auto reply=detail(module(modules,args));paginate(reply,args);return reply;
            }
            RemoteReply reply{"Progress",{}};
            for(const auto& [id,entry]:modules.entries()) if(entry.requested) {
                const auto item=detail(entry);QString text=item.description;
                for(const auto& [label,value]:item.fields)text+="\n"+label+": "+value;
                reply.fields.append({item.title,text});
            }
            if(reply.fields.empty())reply.description="No features enabled.";
            paginate(reply,args);return reply;
        },{module_option(),page_option()},autocomplete},
        {"timers","Show saved cooldowns and harvest times",false,[&modules](const RemoteArguments& args) {
            RemoteReply reply{"Timers",{}};
            const auto append=[&](const extensions::Entry& entry) {
                for(const auto& timer:entry.timers()) reply.fields.append({q(entry.manifest.name)+" · "+q(timer.label),timer_value(timer)});
            };
            if(!args["module"].toString().isEmpty())append(module(modules,args));
            else for(const auto& [id,entry]:modules.entries())if(entry.active())append(entry);
            if(reply.fields.empty())reply.description="No saved timers.";
            paginate(reply,args);
            QList<RemoteCard> cards; for(const auto& [label,value]:reply.fields) cards.append({label,value});
            reply.image=remote_cards(reply.title,cards); reply.fields.clear(); return reply;
        },{module_option(),page_option()},autocomplete}
    };
    const auto setup_complete=[&modules](const QString& key,const RemoteArguments& args) { return setup_choices(modules,key,args); };
    auto required_module=module_option(); required_module.required=true; required_module.description="Feature to configure";
    commands.append({"features","Show farming features or enable and disable one while stopped",true,[&modules](const RemoteArguments& args) {
        if (args.contains("enabled")) {
            const auto id=managed_module(modules,args).manifest.id;
            modules.enable_module(id,args["enabled"].toBool());
        }
        RemoteReply reply{"Features",{}};
        for (const auto& [id,entry]:modules.entries()) if (manageable(entry))
            reply.fields.append({q(entry.manifest.name),entry.requested?entry.active()?"● Enabled":"Needs attention":"○ Disabled"});
        reply.inline_fields=true; return reply;
    },{module_option(),{"enabled","Enable or disable this feature",RemoteOptionType::boolean}},setup_complete});
    commands.append({"settings","View or change settings; running changes apply after the current activity",true,[&modules](const RemoteArguments& args) {
        const auto& entry=managed_module(modules,args);
        const auto id=entry.manifest.id; auto values=modules.module_settings(id);
        RemoteReply reply{q(entry.manifest.name)+" settings",{}};
        if (args.contains("value")) {
            const auto& field=setting(entry,args);
            const auto value=extensions::parse_setting(field,args["value"].toString());
            const auto before=extensions::setting_text(field,values.at(field.id));
            const bool queued=modules.request_settings(id,{{field.id,value}});
            reply.description=(queued?QString("Queued after the current activity. Stop cancels pending changes."):QString("Saved."))
                +"\n"+q(field.label)+": "+before+" → "+extensions::setting_text(field,value);
            values[field.id]=value;
        }
        const auto pending=modules.pending_settings(id);
        for (const auto& [key,value]:pending) values[key]=value;
        const auto* selected=args["setting"].toString().isEmpty()?nullptr:&setting(entry,args);
        for (const auto* field:scoped_settings(entry,args["item"].toString())) if (!selected || selected==field)
            reply.fields.append({q(field->label)+(pending.contains(field->id)?" (queued)":""),extensions::setting_text(*field,values.at(field->id))+q(field->suffix)});
        paginate(reply,args); reply.inline_fields=true; return reply;
    },{required_module,{"item","Field or item to configure; omit for general settings",RemoteOptionType::text,false,{},1,100,true},
       {"setting","Setting name; leave blank to list",RemoteOptionType::text,false,{},1,100,true},
       {"value","New value; use commas for multiple choices",RemoteOptionType::text,false,{},0,1000,true},page_option()},setup_complete});
    commands.append({"priority","Show the running order or move a feature while stopped",true,[&modules](const RemoteArguments& args) {
        auto order=modules.run_order();
        if (args.contains("position")) {
            const auto id=managed_module(modules,args).manifest.id;
            const auto found=std::find(order.begin(),order.end(),id); const auto position=args["position"].toInt();
            if (found==order.end() || position<1 || position>int(order.size())) throw std::invalid_argument("Choose an enabled feature and a position in the current order");
            order.erase(found); order.insert(order.begin()+position-1,id); modules.set_run_order(order);
        }
        RemoteReply reply{"Priority","Gather interrupt can run eligible work ahead of this order."};
        for (std::size_t i=0;i<order.size();++i) reply.fields.append({QString::number(i+1),q(modules.entries().at(order[i]).manifest.name)});
        return reply;
    },{module_option(),{"position","Position in the running order",RemoteOptionType::integer,false,{},1,64}},setup_complete});
    for(const auto& registered:modules.commands().entries()) {
        const auto& declaration=registered.command;
        if(!declaration.remote || !declaration.target_prompt.empty() || declaration.alias.empty())continue;
        RemoteCommand command{q(declaration.alias),q(declaration.description.empty()?declaration.label:declaration.description),declaration.access!=extensions::CommandAccess::read,
            [&modules,owner=registered.owner,id=declaration.id](const RemoteArguments& args) { return command_reply(modules.invoke_module(owner,id,true,command_input(args))); }};
        for(const auto& argument:declaration.arguments) {
            RemoteOption option{q(argument.id),q(argument.description.empty()?argument.label:argument.description),RemoteOptionType::text,argument.required && !argument.initial,{},argument.minimum,argument.maximum};
            if(argument.type==extensions::ArgumentType::integer)option.type=RemoteOptionType::integer;
            else if(argument.type==extensions::ArgumentType::number)option.type=RemoteOptionType::number;
            else if(argument.type==extensions::ArgumentType::boolean)option.type=RemoteOptionType::boolean;
            if(argument.choices.size()<=25)for(const auto& choice:argument.choices)option.choices.append(q(choice));
            else option.autocomplete=true;
            command.options.append(std::move(option));
        }
        std::stable_sort(command.options.begin(),command.options.end(),[](const auto& a,const auto& b){return a.required && !b.required;});
        command.complete=[declaration](const QString& focused,const RemoteArguments& args) {
            RemoteChoices choices;for(const auto& argument:declaration.arguments)if(q(argument.id)==focused)
                for(const auto& value:argument.choices)if(q(value).contains(args[focused].toString(),Qt::CaseInsensitive))choices.append({q(value),q(value)});
            return choices;
        };
        if(std::any_of(commands.begin(),commands.end(),[&](const auto& existing){return existing.name==command.name;}))continue;
        commands.append(std::move(command));
    }
    return commands;
}
}
