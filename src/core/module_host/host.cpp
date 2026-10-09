#include "host.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QRegularExpression>
#include <set>
#include <algorithm>
#include <cmath>

namespace hive::extensions {
namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string exception_message() {
    try { throw; }
    catch (const std::exception& e) { return e.what(); }
    catch (...) { return "Module raised an unexpected error"; }
}
QJsonObject encode(const Settings& settings) {
    QJsonObject object;
    for (const auto& [key,value] : settings) std::visit([&,name=q(key)](const auto& v) {
        using T=std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T,std::string>) object[name]=q(v);
        else if constexpr (std::is_same_v<T,SettingSelection>) {
            QJsonArray items; for (const auto& item : v) items.append(q(item)); object[name]=items;
        }
        else object[name]=v;
    },value);
    return object;
}
Settings decode(const QJsonObject& object) {
    Settings result;
    for (auto it=object.begin(); it!=object.end(); ++it) {
        if (it->isBool()) result[it.key().toStdString()]=it->toBool();
        else if (it->isString()) result[it.key().toStdString()]=it->toString().toStdString();
        else if (it->isDouble() && it->toDouble()==it->toInt()) result[it.key().toStdString()]=it->toInt();
        else if (it->isArray() && it->toArray().size()<=static_cast<qsizetype>(maximum_setting_choices)) {
            SettingSelection items;
            for (const auto& item : it->toArray()) {
                if (!item.isString() || item.toString().size()>80) throw std::runtime_error("Invalid stored selection");
                items.push_back(item.toString().toStdString());
            }
            result[it.key().toStdString()]=std::move(items);
        }
        else throw std::runtime_error("Invalid stored setting type");
    }
    return result;
}
}
void Entry::stop() noexcept {
    if (instance_) { instance_->stop(); instance_.reset(); }
    display.clear();
}
Host::Host(QString state_file,std::function<bool()> session_active,FailureHandler failure_handler)
    : state_file_(std::move(state_file)),session_active_(std::move(session_active)),failure_handler_(std::move(failure_handler)) {}
void Host::require_stopped() const {
    if (session_active_ && session_active_()) throw std::logic_error("Stop the session before changing modules");
}
automation::Launch Host::prepare(const std::string& id) const {
    require_stopped(); order(id);
    const auto& entry=entries_.at(id);
    if (!entry.executable()) throw std::logic_error("Enable an executable module before starting it");
    try {
        auto program=entry.instance_->program();
        if (!program) throw std::runtime_error("Module did not provide an executable program");
        return {id,entry.manifest.permissions,std::move(program)};
    } catch (...) { throw std::runtime_error(exception_message()); }
}
automation::Launch Host::prepare_command(const std::string& id,const std::string& command,const CommandInput& input) const {
    require_stopped(); order(id);
    const auto& entry=entries_.at(id);
    if (!entry.executable()) throw std::logic_error("Enable the module before starting a tool");
    const auto& option=commands_.find(id,command);
    if (!option.launches || option.plot) throw std::invalid_argument("Module does not expose this launch command");
    const auto validated=validate_input(option,input);
    try {
        auto program=entry.instance_->command_program(command,validated);
        if (!program) throw std::runtime_error("Tool did not provide a program");
        return {id,entry.manifest.permissions,std::move(program)};
    } catch (...) { throw std::runtime_error(exception_message()); }
}
CommandRegistry::Handler Host::command_handler(const std::string& owner) {
    return [this,owner](const std::string& id,const CommandInput& input) {
        const auto command=commands_.find(owner,id);
        if (command.launches) {
            if (!command_launcher_) throw std::logic_error("Tool launch is not connected");
            command_launcher_(prepare_command(owner,id,input));return CommandResult{command.label,"Starting."};
        }
        return entries_.at(owner).instance_->execute_command(id,input);
    };
}
CommandResult Host::invoke_command(const std::string& owner,const std::string& command,CommandInput input,bool remote) {
    return commands_.invoke(owner,command,std::move(input),session_active_ && session_active_(),remote);
}
std::vector<RoutineEntry> Host::routine_entries() const {
    require_stopped();
    std::vector<RoutineEntry> result;
    for (const auto& [id,entry] : entries_) {
        if (!entry.executable()) continue;
        if (const auto participation=entry.instance_->routine()) result.push_back({id,entry.revision(),*participation});
    }
    return result;
}
automation::Launch Host::prepare_routine(const RoutineEntry& expected) const {
    order(expected.id);
    const auto& entry=entries_.at(expected.id);
    if (!entry.executable() || entry.revision()!=expected.revision || !entry.instance_->routine())
        throw std::runtime_error("Routine module changed; restart the session");
    auto program=entry.instance_->routine_program();
    if (!program) throw std::runtime_error("Module did not provide a routine program");
    return {expected.id,entry.manifest.permissions,std::move(program)};
}
Host::~Host() { stop_all(); }
void Host::replace_providers(const std::function<void(Host&)>& registration) {
    require_stopped();
    if (!registration) throw std::invalid_argument("Missing provider registration");
    Host staged(state_file_);
    staged.requests_=requests_;
    registration(staged);
    if (!staged.entries_.empty()) throw std::invalid_argument("Provider registration must not discover or activate modules");
    stop_all(); entries_.clear();
    factories_=std::move(staged.factories_); builtins_=std::move(staged.builtins_);
    installable_=std::move(staged.installable_); request_access_=std::move(staged.request_access_);
    request_contracts_=std::move(staged.request_contracts_); requests_->reset(0); requests_->configure(request_contracts_);
}
void Host::register_provider(std::string name,Factory factory,game::RequestGrants requests,bool installable) {
    require_stopped();
    if (requests.size()>64) throw std::invalid_argument("Too many request grants");
    for (const auto& [id,limits] : requests) {
        if (!game::request_identifier(id)) throw std::invalid_argument("Invalid request contract identity");
        game::validate(limits);
    }
    if (!factory || !factories_.emplace(name,std::move(factory)).second) throw std::runtime_error("Duplicate or empty provider");
    if (installable) installable_.insert(name);
    request_access_.emplace(std::move(name),std::move(requests));
}
void Host::register_request_contract(std::string id,std::function<void(const Settings&)> validator) {
    require_stopped();
    if (!entries_.empty()) throw std::logic_error("Register request contracts before discovery");
    if (!game::request_identifier(id) || !validator || request_contracts_.size()>=64
        || !request_contracts_.emplace(std::move(id),std::move(validator)).second)
        throw std::invalid_argument("Invalid or duplicate request contract");
}
void Host::register_module(Manifest manifest,std::string unavailable,SettingsMigration migration) {
    require_stopped();
    static const QRegularExpression identifier("^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*$");
    const auto valid_id=[&](const std::string& id) { return id.size()<=80 && identifier.match(q(id)).hasMatch(); };
    if (!valid_id(manifest.id) || !valid_id(manifest.provider) || !factories_.contains(manifest.provider)
        || manifest.api!=1 || manifest.settings_version<1 || manifest.settings_version>10000
        || manifest.name.empty() || manifest.name.size()>80 || manifest.version.size()>32 || manifest.description.empty() || manifest.description.size()>500
        || !QRegularExpression("^[0-9]+\\.[0-9]+\\.[0-9]+$").match(q(manifest.version)).hasMatch()
        || !manifest.scripts.empty() || !manifest.sources.empty() || !manifest.commands.empty() || manifest.settings.size()>maximum_settings || manifest.dependencies.size()>16
        || manifest.bindings.size()>16 || unavailable.size()>500 || builtins_.contains(manifest.id) || entries_.contains(manifest.id))
        throw std::invalid_argument("Invalid or duplicate compiled module registration");
    if (manifest.routine) validate(*manifest.routine);
    for (auto permission : manifest.permissions) if (permission!=automation::Permission::movement
        && permission!=automation::Permission::tool && permission!=automation::Permission::interact)
        throw std::invalid_argument("Invalid compiled module permission");
    if (manifest.requests.size()>64 || manifest.consumes.size()>64) throw std::invalid_argument("Too many request contracts");
    for (const auto* contracts : {&manifest.requests,&manifest.consumes})
        for (const auto& id : *contracts) if (!game::request_identifier(id)) throw std::invalid_argument("Invalid request contract identity");
    std::set<std::string> dependencies;
    for (const auto& id : manifest.dependencies) if (!valid_id(id) || !dependencies.insert(id).second)
        throw std::invalid_argument("Invalid compiled module dependency");
    for (const auto& setting : manifest.settings) {
        if (setting.ordered && !std::holds_alternative<SettingSelection>(setting.initial))
            throw std::invalid_argument("Only selections can be ordered");
        if (!valid_id(setting.id) || setting.label.empty() || setting.label.size()>80
            || setting.minimum>setting.maximum || setting.minimum<-1000000 || setting.maximum>1000000 || setting.choices.size()>maximum_setting_choices
            || setting.decimals>3 || (setting.decimals && !std::holds_alternative<int>(setting.initial)))
            throw std::invalid_argument("Invalid compiled module setting");
        std::set<std::string> choices;
        for (const auto& choice : setting.choices) if (choice.empty() || choice.size()>80 || !choices.insert(choice).second)
            throw std::invalid_argument("Invalid compiled module choices");
        if ((std::holds_alternative<std::string>(setting.initial) || std::holds_alternative<SettingSelection>(setting.initial))
            ?setting.choices.empty():!setting.choices.empty())
            throw std::invalid_argument("Incompatible compiled module setting type");
    }
    validate_settings(manifest,defaults(manifest)); validate_page(manifest);
    const auto id=manifest.id; builtins_.emplace(id,Builtin{std::move(manifest),std::move(unavailable),std::move(migration)});
}
std::vector<std::string> Host::order(const std::string& id) const {
    std::vector<std::string> result; std::set<std::string> visiting,visited;
    std::function<void(const std::string&)> visit=[&](const std::string& key) {
        if (visited.contains(key)) return;
        if (!visiting.insert(key).second) throw std::runtime_error("Extension dependency cycle at "+key);
        auto it=entries_.find(key);
        if (it==entries_.end()) throw std::runtime_error("Missing dependency: "+key);
        const auto& entry=it->second;
        if (const auto builtin=builtins_.find(key); builtin!=builtins_.end() && !builtin->second.unavailable.empty())
            throw std::runtime_error(builtin->second.unavailable);
        if (entry.manifest.api!=1) throw std::runtime_error("Unsupported extension API for "+key);
        if (!factories_.contains(entry.manifest.provider)) throw std::runtime_error("Unavailable provider: "+entry.manifest.provider);
        for (const auto& channel : entry.manifest.requests)
            if (!request_contracts_.contains(channel) || !request_access_.at(entry.manifest.provider).contains(channel))
                throw std::runtime_error("Provider has no registered request grant: "+channel);
        for (const auto& channel : entry.manifest.consumes)
            if (!request_contracts_.contains(channel)) throw std::runtime_error("Missing request contract: "+channel);
        if (!entry.config_valid) throw std::runtime_error("Reset or correct stored settings for "+key);
        for (const auto& dep : entry.manifest.dependencies) visit(dep);
        visiting.erase(key); visited.insert(key); result.push_back(key);
    };
    visit(id); return result;
}
void Host::stop_entry(Entry& entry) noexcept {
    if (entry.request_scope_) { requests_->revoke(*entry.request_scope_); entry.request_scope_.reset(); }
    commands_.remove(entry.manifest.id);entry.stop();
}
void Host::stop_all() noexcept {
    std::set<std::string> stopped;
    std::function<void(const std::string&)> visit=[&](const std::string& id) {
        if (!stopped.insert(id).second) return;
        for (auto& [key,entry] : entries_)
            if (std::find(entry.manifest.dependencies.begin(),entry.manifest.dependencies.end(),id)!=entry.manifest.dependencies.end()) visit(key);
        stop_entry(entries_.at(id));
    };
    for (auto& [key,entry] : entries_) visit(key);
}
void Host::discover(const QStringList& roots) {
    require_stopped();
    stop_all(); entries_.clear(); diagnostics_.clear(); saved_={}; state_writable_=true;
    requests_->reset(0); requests_->configure(request_contracts_);
    std::set<std::string> duplicates;
    for (const auto& [id,builtin] : builtins_) entries_.emplace(id,Entry{builtin.manifest,defaults(builtin.manifest)});
    for (const auto& root : roots) {
        auto folders=QDir(root).entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks,QDir::Name);
        if (folders.size()>128) { diagnostics_.push_back("Too many extension folders in "+root.toStdString()); continue; }
        for (const auto& folder : folders) {
            const auto path=folder.filePath()+"/extension.json";
            if (!QFileInfo::exists(path)) continue;
            try {
                if (QFileInfo(path).isSymLink()) throw std::runtime_error("Symlinked manifests are not supported");
                auto manifest=read_manifest(path); auto id=manifest.id;
                if (builtins_.contains(id)) throw std::runtime_error("Package conflicts with a compiled module: "+id);
                if (entries_.contains(id) || duplicates.contains(id)) {
                    entries_.erase(id); duplicates.insert(id); throw std::runtime_error("Duplicate extension ID quarantined: "+id);
                }
                auto settings=defaults(manifest);
                entries_.emplace(id,Entry{std::move(manifest),std::move(settings)});
            } catch (const std::exception& e) { diagnostics_.push_back(path.toStdString()+": "+e.what()); }
        }
    }
    restore();
    for (auto& [id,entry] : entries_) {
        try { order(id); } catch (const std::exception& e) { entry.issue=e.what(); }
    }
    for (auto& [id,entry] : entries_) if (entry.requested) {
        try { activate(id); } catch (const std::exception& e) { entry.issue=e.what(); }
    }
}
void Host::activate(const std::string& id) {
    const auto sorted=order(id);
    std::vector<std::pair<std::string,std::unique_ptr<Module>>> pending;
    auto registered=commands_;
    try {
        for (const auto& key : sorted) {
            auto& entry=entries_.at(key); if (entry.instance_) continue;
            auto instance=factories_.at(entry.manifest.provider)(entry.manifest);
            if (!instance) throw std::runtime_error("Provider returned an empty module");
            pending.emplace_back(key,std::move(instance)); pending.back().second->start(entry.settings);
            registered.replace(key,pending.back().second->commands(),command_handler(key));
            if (!pending.back().second->executable() && !entry.manifest.permissions.empty()) throw std::runtime_error("Read-only modules cannot request input permissions");
        }
    } catch (...) {
        auto message=exception_message();
        for (auto it=pending.rbegin(); it!=pending.rend(); ++it) it->second->stop();
        throw std::runtime_error(message);
    }
    for (auto& [key,instance] : pending) { auto& entry=entries_.at(key); entry.instance_=std::move(instance); entry.revision_=next_revision_++; entry.issue.clear(); }
    commands_=std::move(registered);
}
void Host::enable(const std::string& id) {
    require_stopped();
    auto sorted=order(id);
    std::map<std::string,bool> previous;
    std::set<std::string> active;
    for (auto& [key,entry] : entries_) if (entry.instance_) active.insert(key);
    for (const auto& key : sorted) { auto& entry=entries_.at(key); previous[key]=entry.requested; entry.requested=true; }
    try { activate(id); persist(); }
    catch (...) {
        for (auto it=sorted.rbegin(); it!=sorted.rend(); ++it) if (!active.contains(*it)) stop_entry(entries_.at(*it));
        for (const auto& [key,value] : previous) entries_.at(key).requested=value;
        throw;
    }
}
void Host::stop_dependents(const std::string& id,const std::string& reason) {
    std::set<std::string> visited;
    std::function<void(const std::string&)> visit=[&](const std::string& key) {
        if (!visited.insert(key).second) return;
        for (auto& [other,entry] : entries_)
            if (std::find(entry.manifest.dependencies.begin(),entry.manifest.dependencies.end(),key)!=entry.manifest.dependencies.end()) visit(other);
        auto& entry=entries_.at(key); stop_entry(entry); if (entry.requested) entry.issue=reason;
        if (!reason.empty() && failure_handler_) {
            try { failure_handler_(key,reason); }
            catch (...) { diagnostics_.push_back("Could not notify the session of module failure: "+key); }
        }
    };
    visit(id);
}
void Host::fail_module(const std::string& id,const std::string& reason) {
    stop_dependents(id,reason.empty()?"Module could not continue":reason);
}
bool Host::pending_work(const std::string& id,const SessionView& view) {
    try { return entries_.at(id).pending_work(view); }
    catch (...) { fail_module(id,exception_message()); return false; }
}
void Host::disable(const std::string& id) {
    require_stopped();
    if (!entries_.contains(id)) throw std::runtime_error("Unknown extension");
    std::set<std::string> affected{id};
    bool changed=true;
    while (changed) {
        changed=false;
        for (const auto& [key,entry] : entries_) for (const auto& dep : entry.manifest.dependencies)
            if (affected.contains(dep) && affected.insert(key).second) changed=true;
    }
    std::map<std::string,bool> previous;
    for (const auto& key : affected) { auto& entry=entries_.at(key); previous[key]=entry.requested; entry.requested=false; }
    try { persist(); } catch (...) { for (const auto& [key,value] : previous) entries_.at(key).requested=value; throw; }
    stop_dependents(id,{});
}
void Host::configure(const std::string& id,Settings settings) {
    require_stopped();
    auto& entry=entries_.at(id); validate_settings(entry.manifest,settings);
    if (entry.instance_ && entry.config_valid && entry.settings==settings) { persist(); return; }
    std::unique_ptr<Module> replacement;auto registered=commands_;
    if (entry.instance_) {
        try {
            replacement=factories_.at(entry.manifest.provider)(entry.manifest);
            if (!replacement) throw std::runtime_error("Provider returned an empty module");
            replacement->start(settings);
            registered.replace(id,replacement->commands(),command_handler(id));
            if (!replacement->executable() && !entry.manifest.permissions.empty()) throw std::runtime_error("Read-only modules cannot request input permissions");
        } catch (...) {
            auto message=exception_message(); if (replacement) replacement->stop(); throw std::runtime_error(message);
        }
    }
    auto previous=entry.settings; const bool was_valid=entry.config_valid;
    entry.settings=std::move(settings); entry.config_valid=true;
    try { persist(); } catch (...) { entry.settings=std::move(previous); entry.config_valid=was_valid; if (replacement) replacement->stop(); throw; }
    stop_entry(entry); entry.instance_=std::move(replacement); if (entry.instance_) entry.revision_=next_revision_++; entry.issue.clear();
    commands_=std::move(registered);
}
void Host::update(const SessionView& view) {
    const auto generation=view.status.state==RunState::running?view.generation:0;
    if (generation!=requests_->generation()) {
        requests_->reset(generation);
        for (auto& [_,entry] : entries_) entry.request_scope_.reset();
    }
    const auto inbox=requests_->channels(view.now,generation);
    std::set<std::string> updated;
    for (const auto& [id,entry] : entries_) {
        if (!entry.instance_) continue;
        for (const auto& key : order(id)) {
            auto& current=entries_.at(key);
            if (!current.instance_ || !updated.insert(key).second) continue;
            try {
                auto supplied=view; supplied.requests.clear();
                for (const auto& channel : current.manifest.consumes)
                    if (const auto it=inbox.find(channel); it!=inbox.end()) supplied.requests.emplace(channel,it->second);
                current.instance_->update(supplied); auto display=current.instance_->metrics();
                if (display.size()>128) throw std::runtime_error("Too many display metrics");
                for (const auto& metric : display) if (metric.label.size()>160 || metric.value.size()>4096 || metric.group.size()>160
                    || (metric.progress && (!std::isfinite(*metric.progress) || *metric.progress<0 || *metric.progress>1)))
                    throw std::runtime_error("Invalid display metric");
                current.display=std::move(display);
                if (generation) {
                    const auto batch=current.instance_->request_channels();
                    for (const auto& [channel,_] : batch.channels) if (!current.manifest.requests.contains(channel))
                        throw std::runtime_error("Module published an undeclared request contract: "+channel);
                    if (!current.manifest.requests.empty()) {
                        if (!current.request_scope_) {
                            game::RequestGrants grants;
                            for (const auto& channel : current.manifest.requests)
                                grants.emplace(channel,request_access_.at(current.manifest.provider).at(channel));
                            current.request_scope_=requests_->grant_channels(key,std::move(grants));
                        }
                        requests_->replace_channels(*current.request_scope_,batch,view.now);
                    }
                }
            }
            catch (const std::exception& e) { stop_dependents(key,"Extension stopped: "+std::string(e.what())); }
            catch (...) { stop_dependents(key,"Extension stopped after an unexpected error"); }
        }
    }
}
std::vector<Metric> Host::metrics(const std::string& id) const {
    return entries_.at(id).display;
}
std::vector<game::FieldGoal> Host::field_goals(Time now,std::uint64_t generation) const {
    return requests_->snapshot(now,generation);
}
void Host::persist() const {
    if (!state_writable_) throw std::runtime_error("Extension settings file could not be read; repair it before saving changes");
    auto saved=saved_;
    for (const auto& [id,entry] : entries_) {
        if (!entry.config_valid) continue;
        saved[q(id)]=QJsonObject{{"version",entry.manifest.settings_version},{"enabled",entry.requested},{"values",encode(entry.settings)}};
    }
    if (!QDir().mkpath(QFileInfo(state_file_).absolutePath())) throw std::runtime_error("Cannot create extension settings directory");
    QSaveFile file(state_file_); const auto data=QJsonDocument(QJsonObject{{"version",1},{"extensions",saved}}).toJson();
    if (data.size()>1024*1024) throw std::runtime_error("Extension settings exceed the 1 MiB limit");
    if (!file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit()) throw std::runtime_error("Could not save extension settings");
}
void Host::restore() {
    if (!QFileInfo::exists(state_file_)) return;
    QFile file(state_file_); QJsonParseError error;
    if (!file.open(QIODevice::ReadOnly) || file.size()>1024*1024) { state_writable_=false; diagnostics_.push_back("Cannot read extension settings"); return; }
    auto doc=QJsonDocument::fromJson(file.readAll(),&error); auto object=doc.object();
    if (error.error!=QJsonParseError::NoError || object["version"]!=QJsonValue(1) || !object["extensions"].isObject()) {
        state_writable_=false; diagnostics_.push_back("Invalid extension settings file; original file preserved"); return;
    }
    saved_=object["extensions"].toObject();
    for (auto& [id,entry] : entries_) {
        if (!saved_.contains(q(id))) continue;
        try {
            auto saved=saved_[q(id)].toObject();
            const auto version=saved["version"].toInt(0);
            if (version<1 || version>10000 || saved["version"]!=QJsonValue(version)
                || !saved["enabled"].isBool() || !saved["values"].isObject()) throw std::runtime_error("Invalid stored settings structure");
            auto values=decode(saved["values"].toObject());
            if (version!=entry.manifest.settings_version) {
                const auto builtin=builtins_.find(id);
                if (version>entry.manifest.settings_version || builtin==builtins_.end() || !builtin->second.migration)
                    throw std::runtime_error("Settings version changed; reset required");
                values=builtin->second.migration(version,values);
            }
            auto settings=defaults(entry.manifest);
            for (const auto& [key,value] : values) settings[key]=value;
            validate_settings(entry.manifest,settings);
            entry.settings=std::move(settings); entry.requested=saved["enabled"].toBool();
        } catch (const std::exception& e) { entry.config_valid=false; entry.issue=e.what(); }
        catch (...) { entry.config_valid=false; entry.issue="Settings migration failed"; }
    }
}
}
