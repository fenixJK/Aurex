#include "host.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include <set>
#include <algorithm>
#include <limits>

namespace hive::extensions {
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void keys(const QJsonObject& object, const QStringList& allowed) {
    for (auto i=object.begin(); i!=object.end(); ++i)
        if (!allowed.contains(i.key())) throw std::runtime_error("Unknown manifest field: "+i.key().toStdString());
}
std::string text(const QJsonObject& object, const char* key, int limit=160) {
    auto value=object.value(key);
    require(value.isString() && !value.toString().trimmed().isEmpty() && value.toString().size()<=limit,"Missing or invalid manifest text");
    return value.toString().toStdString();
}
int integer(const QJsonValue& value, int low, int high) {
    require(value.isDouble() && value.toDouble()>=low && value.toDouble()<=high && value.toDouble()==value.toInt(),"Invalid manifest integer");
    return value.toInt();
}
bool identifier(const std::string& value) {
    static const QRegularExpression pattern("^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*$");
    return value.size()<=80 && pattern.match(QString::fromStdString(value)).hasMatch();
}
}
Manifest read_manifest(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly) && file.size()<=65536,"Cannot read manifest (maximum 64 KiB)");
    QJsonParseError error;
    auto doc=QJsonDocument::fromJson(file.readAll(),&error);
    require(error.error==QJsonParseError::NoError && doc.isObject(),"Manifest must be a JSON object");
    const auto obj=doc.object();
    keys(obj,{"id","name","version","description","api","settingsVersion","provider","dependencies","settings","bindings","scripts","permissions","requests","routine","consumes","page","commands"});
    Manifest m;
    m.id=text(obj,"id",80); require(identifier(m.id),"Invalid extension ID");
    m.name=text(obj,"name",80); m.version=text(obj,"version",32); m.description=text(obj,"description",500);
    require(QRegularExpression("^[0-9]+\\.[0-9]+\\.[0-9]+$").match(QString::fromStdString(m.version)).hasMatch(),"Version must have major.minor.patch form");
    m.api=integer(obj["api"],1,10000); m.settings_version=integer(obj["settingsVersion"],1,10000);
    m.provider=text(obj,"provider",80); require(identifier(m.provider),"Invalid provider ID");
    if (obj.contains("routine")) {
        require(m.provider=="script" && obj["routine"].isObject(),"Only executable script packages can declare a routine");
        const auto routine=obj["routine"].toObject(); keys(routine,{"priority","recheckSeconds","fallback","interruptGather"});
        require(!routine.contains("fallback") || routine["fallback"].isBool(),"Routine fallback must be a boolean");
        m.routine=RoutineParticipation{integer(routine["priority"],0,100),Time(integer(routine["recheckSeconds"],1,86400)*1000),routine["fallback"].toBool(false)};
        require(!routine.contains("interruptGather") || routine["interruptGather"].isBool(),"Gather interrupt flag must be boolean");
        m.routine->interrupt_gather=routine["interruptGather"].toBool(true);
        validate(*m.routine);
    }
    for (const auto* key : {"requests","consumes"}) if (obj.contains(key)) {
        require(obj[key].isArray() && obj[key].toArray().size()<=64,"Request contracts must be a bounded array");
        auto& contracts=std::string_view(key)=="requests"?m.requests:m.consumes;
        for (const auto& request : obj[key].toArray()) {
            require(request.isString() && identifier(request.toString().toStdString()),"Invalid request contract identity");
            require(contracts.insert(request.toString().toStdString()).second,"Duplicate request contract");
        }
    }
    if (m.provider=="dashboard" || m.provider=="script") require(m.requests.empty(),"This provider does not publish module requests");
    if (obj.contains("permissions")) {
        require(obj["permissions"].isArray(),"Permissions must be an array");
        for (const auto& permission : obj["permissions"].toArray()) {
            require(permission.isString(),"Invalid permission");
            const auto name=permission.toString();
            require(name=="movement" || name=="tool" || name=="interact","Unsupported feature permission");
            const auto value=name=="movement"?automation::Permission::movement:name=="tool"?automation::Permission::tool:automation::Permission::interact;
            require(m.permissions.insert(value).second,"Duplicate permission");
        }
    }
    if (m.provider=="dashboard") require(m.permissions.empty(),"Dashboards cannot request game input");
    if (m.provider=="script-contributor") {
        require(m.permissions.empty(),"Contributors cannot request game input");
        require(!m.requests.empty() || !m.consumes.empty(),"Contributors must declare request contracts");
    }
    if (m.provider=="script" || m.provider=="script-contributor") {
        require(obj.contains("permissions"),"Script features must declare permissions, including an empty list when none are needed");
        require(obj["scripts"].isObject(),"Script feature needs a scripts map");
        const auto scripts=obj["scripts"].toObject();
        require(scripts.contains("main") && scripts.size()<=17,"Missing main script or too many scripts");
        std::size_t total{}; std::set<QString> files;
        for (auto it=scripts.begin();it!=scripts.end();++it) {
            require(identifier(it.key().toStdString()) && it->isString(),"Invalid script name");
            const auto name=it->toString();
            require(QRegularExpression("^[a-z][a-z0-9-]*\\.lua$").match(name).hasMatch() && files.insert(name.toLower()).second,"Script paths must be unique local .lua filenames");
            const auto source_path=QFileInfo(path).dir().filePath(name);
            QFile script(source_path); require(!QFileInfo(source_path).isSymLink() && script.open(QIODevice::ReadOnly) && script.size()<=65536,"Cannot read script (maximum 64 KiB; no symlinks)");
            const auto data=script.readAll(); total+=data.size(); require(total<=262144,"Script package exceeds 256 KiB");
            m.scripts[it.key().toStdString()]=name.toStdString();
            m.sources[it.key().toStdString()]=data.toStdString();
        }
    } else require(!obj.contains("scripts"),"Only script providers can declare scripts");
    if(obj.contains("commands")) {
        require(m.provider=="script" && obj["commands"].isArray() && obj["commands"].toArray().size()<=64,"Only script packages can declare commands");
        for(const auto& value:obj["commands"].toArray()) {
            require(value.isObject(),"Command must be an object");const auto item=value.toObject();
            keys(item,{"id","label","description","alias","remote","script","arguments"});
            Command command;command.id=text(item,"id",32);command.label=text(item,"label",100);
            command.script=text(item,"script",80);command.plot=false;command.launches=true;
            require(m.sources.contains(command.script),"Command script is not declared in scripts");
            if(item.contains("description"))command.description=text(item,"description",100);
            if(item.contains("alias"))command.alias=text(item,"alias",32);
            require(!item.contains("remote") || item["remote"].isBool(),"Command remote flag must be boolean");command.remote=item["remote"].toBool();
            if(item.contains("arguments")) {
                require(item["arguments"].isArray() && item["arguments"].toArray().size()<=24,"Invalid command arguments");
                for(const auto& value:item["arguments"].toArray()) {
                    require(value.isObject(),"Command argument must be an object");const auto arg=value.toObject();
                    keys(arg,{"id","label","description","type","required","default","min","max","choices"});
                    CommandArgument field;field.id=text(arg,"id",32);field.label=text(arg,"label",100);
                    if(arg.contains("description"))field.description=text(arg,"description",100);
                    const auto type=text(arg,"type",16);
                    if(type=="text")field.type=ArgumentType::text;
                    else if(type=="integer")field.type=ArgumentType::integer;
                    else if(type=="number")field.type=ArgumentType::number;
                    else if(type=="boolean")field.type=ArgumentType::boolean;
                    else throw std::runtime_error("Unsupported command argument type");
                    require(!arg.contains("required") || arg["required"].isBool(),"Required flag must be boolean");field.required=arg["required"].toBool();
                    require(!arg.contains("min") || arg["min"].isDouble(),"Invalid command minimum");
                    require(!arg.contains("max") || arg["max"].isDouble(),"Invalid command maximum");
                    field.minimum=arg["min"].toDouble(field.minimum);field.maximum=arg["max"].toDouble(field.maximum);
                    if(arg.contains("choices")) {
                        require(arg["choices"].isArray(),"Invalid command choices");
                        for(const auto& choice:arg["choices"].toArray()) {require(choice.isString(),"Command choice must be text");field.choices.push_back(choice.toString().toStdString());}
                    }
                    if(arg.contains("default")) {
                        const auto initial=arg["default"];
                        if(initial.isString())field.initial=initial.toString().toStdString();
                        else if(initial.isBool())field.initial=initial.toBool();
                        else if(initial.isDouble()) {
                            if(field.type==ArgumentType::integer)field.initial=integer(initial,std::numeric_limits<int>::min(),std::numeric_limits<int>::max());
                            else field.initial=initial.toDouble();
                        } else throw std::runtime_error("Invalid command default");
                    }
                    command.arguments.push_back(std::move(field));
                }
            }
            m.commands.push_back(std::move(command));
        }
        validate_commands(m.commands);
    }
    require(obj["dependencies"].isArray() && obj["dependencies"].toArray().size()<=16,"Invalid dependencies");
    std::set<std::string> seen;
    for (const auto& dep : obj["dependencies"].toArray()) {
        require(dep.isString(),"Invalid dependency"); auto id=dep.toString().toStdString();
        require(identifier(id) && seen.insert(id).second,"Invalid or duplicate dependency"); m.dependencies.push_back(id);
    }
    require(obj["settings"].isArray() && obj["settings"].toArray().size()<=static_cast<qsizetype>(maximum_settings),"Invalid settings"); seen.clear();
    for (const auto& value : obj["settings"].toArray()) {
        require(value.isObject(),"Setting must be an object"); auto s=value.toObject();
        keys(s,{"id","label","type","default","min","max","choices","ordered","inline","help","hidden","suffix","decimals","groups","enabled_by","step","step_toggle","checked_step","icons"});
        Setting field; field.id=text(s,"id",80); field.label=text(s,"label",80);
        require(identifier(field.id) && seen.insert(field.id).second,"Invalid or duplicate setting ID");
        const auto type=text(s,"type");
        if (s.contains("decimals")) {
            require(type=="integer","Decimal display requires an integer setting");
            field.decimals=integer(s["decimals"],0,3);
        }
        require(!s.contains("ordered") || (type=="selection" && s["ordered"].isBool()),"Only selections can declare boolean ordered metadata");
        field.ordered=s["ordered"].toBool();
        require(!s.contains("inline") || (type=="selection" && s["inline"].isBool()),"Inline display requires a selection");
        require(!s.contains("hidden") || s["hidden"].isBool(),"Hidden flag must be boolean");
        if(s.contains("suffix")) field.suffix=text(s,"suffix",16);
        field.inline_choices=s["inline"].toBool(); field.hidden=s["hidden"].toBool();
        if(s.contains("help")) field.help=text(s,"help",500);
        if(s.contains("enabled_by")) field.enabled_by=text(s,"enabled_by",80);
        if(s.contains("step")) field.step=integer(s["step"],1,1000000);
        if(s.contains("step_toggle")) field.step_toggle=text(s,"step_toggle",80);
        if(s.contains("checked_step")) field.checked_step=integer(s["checked_step"],1,1000000);
        if (type=="boolean") {
            require(s["default"].isBool() && !s.contains("min") && !s.contains("max") && !s.contains("choices"),"Invalid boolean setting"); field.initial=s["default"].toBool();
        } else if (type=="integer") {
            require(!s.contains("choices"),"Integer setting cannot define choices");
            field.minimum=integer(s["min"],std::numeric_limits<int>::min(),std::numeric_limits<int>::max());
            field.maximum=integer(s["max"],field.minimum,std::numeric_limits<int>::max());
            field.initial=integer(s["default"],field.minimum,field.maximum);
        } else if (type=="choice" || type=="selection") {
            require(s["choices"].isArray() && !s["choices"].toArray().isEmpty() && s["choices"].toArray().size()<=maximum_setting_choices && !s.contains("min") && !s.contains("max"),"Invalid choices");
            std::set<std::string> choices;
            for (const auto& choice : s["choices"].toArray()) {
                require(choice.isString() && !choice.toString().isEmpty() && choice.toString().size()<=80,"Invalid choice");
                auto item=choice.toString().toStdString(); require(choices.insert(item).second,"Duplicate choice"); field.choices.push_back(item);
            }
            if (type=="choice") field.initial=text(s,"default",80);
            else {
                require(s["default"].isArray() && s["default"].toArray().size()<=maximum_setting_choices,"Invalid default selection");
                SettingSelection selected;
                for (const auto& item : s["default"].toArray()) {
                    require(item.isString(),"Invalid selection item"); selected.push_back(item.toString().toStdString());
                }
                field.initial=std::move(selected);
            }
        } else throw std::runtime_error("Unsupported setting type");
        if (s.contains("groups")) {
            require(s["groups"].isArray() && s["groups"].toArray().size()<=20,"Invalid choice groups");
            for (const auto& value:s["groups"].toArray()) {
                require(value.isObject(),"Choice group must be an object");
                const auto group=value.toObject(); keys(group,{"label","choices"});
                ChoiceGroup item{text(group,"label",80),{}};
                require(group["choices"].isArray() && group["choices"].toArray().size()<=maximum_setting_choices,"Invalid grouped choices");
                for (const auto& choice:group["choices"].toArray()) {
                    require(choice.isString(),"Invalid grouped choice"); item.choices.push_back(choice.toString().toStdString());
                }
                field.groups.push_back(std::move(item));
            }
        }
        if (s.contains("icons")) {
            require(s["icons"].isObject(),"Choice icons must be an object");
            const auto icons=s["icons"].toObject();
            for (auto it=icons.begin();it!=icons.end();++it) {
                require(it.value().isString(),"Choice icon must be a resource path");
                field.icons.emplace(it.key().toStdString(),it.value().toString().toStdString());
            }
        }
        m.settings.push_back(std::move(field));
    }
    require(obj["bindings"].isArray() && obj["bindings"].toArray().size()<=16,"Invalid bindings");
    const std::set<std::string> known_signals{"session.state","session.job","session.action","session.completed-actions","session.queued-jobs","session.reason"};
    for (const auto& value : obj["bindings"].toArray()) {
        require(value.isObject(),"Binding must be an object"); auto binding=value.toObject(); keys(binding,{"label","signal"});
        Binding b{text(binding,"label",80),text(binding,"signal",80)};
        require(known_signals.contains(b.signal),"Unknown session signal"); m.bindings.push_back(std::move(b));
    }
    if (obj.contains("page")) {
        require(obj["page"].isObject(),"Page must be an object");
        const auto page=obj["page"].toObject(); keys(page,{"sections","compact"});
        require(page["sections"].isArray() && page["sections"].toArray().size()<=static_cast<qsizetype>(maximum_page_sections),"Invalid page sections");
        m.page=PageLayout{};
        if (page.contains("compact")) {
            require(page["compact"].isBool(),"Invalid compact page preference");
            m.page->compact=page["compact"].toBool();
        }
        for (const auto& value : page["sections"].toArray()) {
            require(value.isObject(),"Page section must be an object");
            const auto section=value.toObject(); keys(section,{"id","title","description","settings","metrics","columns","collapsed","collection","choice","row","tool","tab","order_group","order_setting"});
            PageSection item; item.id=text(section,"id",80); item.title=text(section,"title",80);
            if(section.contains("tool")) item.tool=text(section,"tool",80);
            if(section.contains("tab")) item.tab=text(section,"tab",80);
            require(item.tool.empty() || item.tab.empty(),"A section cannot be both a tab and a tool");
            if (section.contains("description")) item.description=text(section,"description",500);
            if (section.contains("settings")) {
                require(section["settings"].isArray() && section["settings"].toArray().size()<=24,"Invalid page setting references");
                for (const auto& setting : section["settings"].toArray()) {
                    require(setting.isString(),"Page setting reference must be text"); item.settings.push_back(setting.toString().toStdString());
                }
            }
            require(!section.contains("metrics") || section["metrics"].isBool(),"Page metrics flag must be boolean");
            item.metrics=section["metrics"].toBool();
            require(!section.contains("collapsed") || section["collapsed"].isBool(),"Page collapsed flag must be boolean");
            item.collapsed=section["collapsed"].toBool();
            if (section.contains("columns")) item.columns=static_cast<unsigned>(integer(section["columns"],1,4));
            if (section.contains("collection")) item.collection=text(section,"collection",80);
            if (section.contains("choice")) item.choice=text(section,"choice",80);
            require(!section.contains("row") || section["row"].isBool(),"Page row flag must be boolean");
            item.row=section["row"].toBool();
            if(section.contains("order_group")) item.order_group=text(section,"order_group",80);
            if(section.contains("order_setting")) item.order_setting=text(section,"order_setting",80);
            m.page->sections.push_back(std::move(item));
        }
    }
    validate_settings(m,defaults(m)); validate_page(m); return m;

}
void validate_page(const Manifest& manifest) {
    for (const auto& setting:manifest.settings) if (!setting.icons.empty()) {
        require(setting.inline_choices && std::holds_alternative<SettingSelection>(setting.initial)
            && setting.icons.size()==setting.choices.size(),"Icon selection needs an icon for each choice");
        for (const auto& [choice,path]:setting.icons)
            require(std::find(setting.choices.begin(),setting.choices.end(),choice)!=setting.choices.end()
                && path.starts_with(":/") && path.size()<=200 && path.find("..") == std::string::npos,
                "Invalid choice icon resource");
    }
    for (const auto& field:manifest.settings) if (field.step!=1 || !field.step_toggle.empty() || field.checked_step!=1) {
        require(std::holds_alternative<int>(field.initial) && field.minimum>=0 && field.step>0 && field.checked_step>0,
            "Numeric steps require a nonnegative integer setting and positive intervals");
        for (const int step:{field.step,field.step_toggle.empty()?field.step:field.checked_step})
            require(step<=1000000 && (static_cast<long long>(field.minimum)+step-1)/step<=field.maximum/step,"Numeric step has no value within the setting range");
        require(!field.step_toggle.empty() || field.checked_step==1,"Checked step requires a checkbox");
        if (!field.step_toggle.empty()) {
            const auto source=std::find_if(manifest.settings.begin(),manifest.settings.end(),[&](const auto& setting) { return setting.id==field.step_toggle; });
            require(source!=manifest.settings.end() && !source->hidden && std::holds_alternative<bool>(source->initial),"Numeric step must refer to a visible checkbox");
        }
    }
    for (const auto& setting:manifest.settings) if (!setting.enabled_by.empty()) {
        const auto source=std::find_if(manifest.settings.begin(),manifest.settings.end(),[&](const auto& field) { return field.id==setting.enabled_by; });
        require(source!=manifest.settings.end() && source->id!=setting.id && !source->hidden
            && source->enabled_by.empty() && std::holds_alternative<bool>(source->initial),"Setting dependency must be an independent visible checkbox");
    }
    for (const auto& setting:manifest.settings) if (!setting.groups.empty()) {
        require(setting.inline_choices && !setting.ordered && std::holds_alternative<SettingSelection>(setting.initial)
            && setting.groups.size()<=20,"Groups require an inline unordered selection");
        std::set<std::string> labels,choices;
        for (const auto& group:setting.groups) {
            require(!group.label.empty() && group.label.size()<=80 && labels.insert(group.label).second && !group.choices.empty(),"Invalid choice group");
            for (const auto& choice:group.choices)
                require(std::find(setting.choices.begin(),setting.choices.end(),choice)!=setting.choices.end() && choices.insert(choice).second,"Unknown or repeated grouped choice");
        }
        require(choices.size()==setting.choices.size(),"Groups must include every choice");
    }
    if (!manifest.page) return;
    const auto& sections=manifest.page->sections;
    require(!sections.empty() && sections.size()<=maximum_page_sections,"Page section count is outside the supported range");
    std::set<std::string> ids,settings,known;
    for (const auto& setting : manifest.settings) known.insert(setting.id);
    unsigned metrics=0;
    for (const auto& section : sections) {
        require(identifier(section.id) && ids.insert(section.id).second && !section.title.empty() && section.title.size()<=80
            && section.description.size()<=500 && section.columns>=1 && section.columns<=4,"Invalid page section");
        require(section.tool.size()<=80 && section.tab.size()<=80
            && (section.collection.empty() || (section.tool.empty() && section.tab.empty())),"Invalid section destination");
        require(section.collection.empty()==section.choice.empty(),"Collection sections need a choice");
        if (!section.collection.empty()) {
            const auto owner=std::find_if(manifest.settings.begin(),manifest.settings.end(),[&](const auto& s) { return s.id==section.collection; });
            require(owner!=manifest.settings.end() && std::holds_alternative<SettingSelection>(owner->initial) && owner->ordered
                && std::find(owner->choices.begin(),owner->choices.end(),section.choice)!=owner->choices.end(),"Invalid collection section");
            require(std::find(section.settings.begin(),section.settings.end(),section.collection)==section.settings.end(),"Collection cannot contain itself");
        }
        require(section.order_group.empty()==section.order_setting.empty(),"Ordered rows need a group and priority setting");
        if(!section.order_group.empty()) {
            const auto field=std::find_if(manifest.settings.begin(),manifest.settings.end(),[&](const auto& s) { return s.id==section.order_setting; });
            require(identifier(section.order_group) && section.row && !section.collapsed && section.tool.empty() && section.collection.empty()
                && field!=manifest.settings.end() && field->hidden && std::holds_alternative<int>(field->initial)
                && std::find(section.settings.begin(),section.settings.end(),field->id)!=section.settings.end()
                && field->minimum<=1 && field->maximum>=std::count_if(sections.begin(),sections.end(),[&](const auto& row) { return row.order_group==section.order_group && row.tab==section.tab; }),"Invalid ordered row setting");
        }
        metrics+=section.metrics;
        require(metrics<=1,"Page can contain one live metrics section");
        for (const auto& id : section.settings)
            require(known.contains(id) && settings.insert(id).second,"Unknown or repeated setting on module page");
    }
    require(settings==known,"Module page must expose every declared setting");
}
Settings defaults(const Manifest& m) { Settings result; for (const auto& s : m.settings) result.emplace(s.id,s.initial); return result; }
void validate_settings(const Manifest& m,const Settings& settings) {
    require(settings.size()==m.settings.size(),"Settings do not match the extension schema");
    for (const auto& s : m.settings) {
        auto it=settings.find(s.id); require(it!=settings.end() && it->second.index()==s.initial.index(),"Missing setting or wrong type");
        if (const auto* n=std::get_if<int>(&it->second)) require(*n>=s.minimum && *n<=s.maximum,"Setting outside allowed range");
        if (const auto* str=std::get_if<std::string>(&it->second)) require(std::find(s.choices.begin(),s.choices.end(),*str)!=s.choices.end(),"Unknown setting choice");
        if (const auto* selected=std::get_if<SettingSelection>(&it->second)) {
            require(selected->size()<=s.choices.size(),"Too many selected choices");
            std::set<std::string> unique;
            for (const auto& item : *selected) require(unique.insert(item).second
                && std::find(s.choices.begin(),s.choices.end(),item)!=s.choices.end(),"Unknown or duplicate selected choice");
        }
    }
}
}
