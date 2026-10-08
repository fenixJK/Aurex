#include "hive/reporting/remote.hpp"
#include "hive/reporting/discord.hpp"
#include "message.hpp"
#include <QBuffer>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QDeadlineTimer>
#include <QRegularExpression>
#include <QSysInfo>
#include <algorithm>
#include <cmath>

namespace hive::reporting {
namespace {
bool snowflake(const QString& value) { return QRegularExpression("^[0-9]{16,22}$").match(value).hasMatch(); }
bool token(const QString& value) { return QRegularExpression("^[A-Za-z0-9._-]{20,256}$").match(value).hasMatch(); }
QUrl gateway(QUrl value=QUrl("wss://gateway.discord.gg")) {
    if (value.scheme()!="wss" || !QRegularExpression("^gateway(?:-[a-z0-9-]+)?\\.discord\\.gg$").match(value.host()).hasMatch()
        || !value.userInfo().isEmpty() || value.port(-1)!=-1) return {};
    value.setPath("/"); value.setFragment({}); value.setQuery("v=10&encoding=json"); return value;
}
QJsonObject message(const QString& text) {
    return {{"content",redact(text).left(1900)},{"allowed_mentions",QJsonObject{{"parse",QJsonArray{}}}}};
}
QJsonArray command_options(const QJsonArray& options) {
    QJsonArray result;
    for(const auto& value:options) {
        const auto option=value.toObject();
        QJsonObject normalized;
        for(const auto* key:{"type","name","description","min_value","max_value"})
            if(option.contains(key))normalized[key]=option[key];
        normalized["required"]=option["required"].toBool();
        normalized["autocomplete"]=option["autocomplete"].toBool();
        if(option["type"].toInt()==3) {
            normalized["min_length"]=option["min_length"].toInt(0);
            normalized["max_length"]=option["max_length"].toInt(6000);
        }
        QJsonArray choices;
        for(const auto& item:option["choices"].toArray()) {
            const auto choice=item.toObject();choices.append(QJsonObject{{"name",choice["name"]},{"value",choice["value"]}});
        }
        normalized["choices"]=choices;result.append(normalized);
    }
    return result;
}
}
QString validate(const RemoteSettings& settings) {
    if (!settings.enabled) return {};
    if (!token(settings.token)) return "Enter the bot token from the Discord Developer Portal.";
    if (!snowflake(settings.user)) return "Enter your Discord user ID.";
    if (!snowflake(settings.channel)) return "Enter the channel ID where commands are allowed.";
    if (!QRegularExpression("^[a-z0-9][a-z0-9-]{0,31}$").match(settings.device).hasMatch())
        return "Use a device name with up to 32 lowercase letters, numbers or hyphens.";
    return {};
}
QJsonObject remote_command_definition(const RemoteCommand& command) {
    QJsonArray options{QJsonObject{{"type",3},{"name","device"},{"description","Device name shown in Remote control"},
        {"required",true},{"min_length",1},{"max_length",32},{"autocomplete",true}}};
    for (const auto& option:command.options) {
        QJsonObject field{{"type",int(option.type)},{"name",option.name},{"description",option.description}};
        if(option.required)field["required"]=true;
        if (option.type==RemoteOptionType::text) {
            field["min_length"]=option.minimum; field["max_length"]=option.maximum;
            if (!option.choices.isEmpty()) {
                QJsonArray choices;
                for (const auto& choice:option.choices) choices.append(QJsonObject{{"name",choice},{"value",choice}});
                field["choices"]=choices;
            } else if (option.autocomplete) field["autocomplete"]=true;
        } else if ((option.type==RemoteOptionType::integer || option.type==RemoteOptionType::number)) { field["min_value"]=option.minimum; field["max_value"]=option.maximum; }
        options.append(field);
    }
    return {{"name",command.name},{"description",command.description},{"type",1},{"options",options}};
}
DiscordRemote::DiscordRemote(QList<RemoteCommand> commands,QObject* parent,QNetworkAccessManager* network)
    :QObject(parent),commands_(std::move(commands)),network_(network?network:new QNetworkAccessManager(this)) {
    set_commands(std::move(commands_));
    retry_.setSingleShot(true); deadline_.setSingleShot(true); heartbeat_.setSingleShot(true);
    socket_.setMaxAllowedIncomingMessageSize(1024*1024);
    connect(&socket_,&QWebSocket::textMessageReceived,this,[this](const QString& packet) { receive(packet); });
    connect(&socket_,&QWebSocket::disconnected,this,[this] {
        heartbeat_.stop(); deadline_.stop(); live_=false;
        if (!settings_.enabled || blocked_) return;
        const int code=int(socket_.closeCode());
        if (code==4004) { fail("Bot token rejected. Update it in Remote control."); return; }
        if (code==4010 || code==4011 || code==4012 || code==4013 || code==4014) { fail("Discord rejected the bot connection settings."); return; }
        if (code==4007 || code==4009) { session_.clear(); sequence_=-1; resume_url_=QUrl{}; }
        reconnect();
    });
    connect(&socket_,&QWebSocket::errorOccurred,this,[this](QAbstractSocket::SocketError) { reconnect(); });
    connect(&heartbeat_,&QTimer::timeout,this,[this] {
        if (awaiting_ack_) { socket_.abort(); reconnect(); return; }
        heartbeat(); heartbeat_.start(heartbeat_ms_);
    });
    connect(&retry_,&QTimer::timeout,this,[this] { live_?bootstrap():open_gateway(); });
    connect(&deadline_,&QTimer::timeout,this,[this] { socket_.abort(); reconnect(); });
}
void DiscordRemote::set_commands(QList<RemoteCommand> commands) {
    if(commands.size()>99)throw std::invalid_argument("Too many Discord commands");
    QSet<QString> names;
    for (const auto& command:commands) {
        if (!QRegularExpression("^[a-z][a-z0-9-]{0,31}$").match(command.name).hasMatch() || command.name=="help"
            || names.contains(command.name) || command.description.isEmpty() || command.description.size()>100 || !command.run)
            throw std::invalid_argument("Invalid or duplicate remote command");
        names.insert(command.name);
        QSet<QString> fields{"device"};bool optional=false;
        if (command.options.size()>24) throw std::invalid_argument("Too many command options");
        for (const auto& field:command.options) {
            const bool type=field.type==RemoteOptionType::text || field.type==RemoteOptionType::integer || field.type==RemoteOptionType::boolean || field.type==RemoteOptionType::number;
            if (!type || !QRegularExpression("^[a-z][a-z0-9-]{0,31}$").match(field.name).hasMatch() || fields.contains(field.name)
                || field.description.isEmpty() || field.description.size()>100 || (optional && field.required)
                || !std::isfinite(field.minimum) || !std::isfinite(field.maximum) || field.minimum>field.maximum || field.choices.size()>25
                || (field.type==RemoteOptionType::text && (field.minimum<0 || field.maximum>6000 || std::floor(field.minimum)!=field.minimum || std::floor(field.maximum)!=field.maximum))
                || ((field.type==RemoteOptionType::integer || field.type==RemoteOptionType::number) && (field.minimum<-9007199254740992. || field.maximum>9007199254740992.))
                || (field.type==RemoteOptionType::integer && (std::floor(field.minimum)!=field.minimum || std::floor(field.maximum)!=field.maximum))
                || (field.autocomplete && (!command.complete || field.type!=RemoteOptionType::text || !field.choices.isEmpty())))
                throw std::invalid_argument("Invalid command option");
            optional|=!field.required;fields.insert(field.name);
        }
    }
    commands.append({"help","Show commands or help with one command",false,[this](const RemoteArguments& args) {
        RemoteReply reply{"Commands","Use the device name `"+settings_.device+"` with each command."};
        const auto selected=args["command"].toString();
        const int pages=std::max(1,int((commands_.size()+19)/20)),page=std::clamp(args["page"].toInt(1),1,pages);
        int index=0;
        for (const auto& command:commands_) if (selected.isEmpty()?(index++/20==page-1):selected==command.name) {
            reply.fields.append({"/"+command.name,command.description});
            if (!selected.isEmpty()) for (const auto& option:command.options)
                reply.fields.append({option.name+(option.required?" (required)":" (optional)"),option.description});
        }
        if(selected.isEmpty() && pages>1)reply.description+=" Page "+QString::number(page)+" of "+QString::number(pages)+".";
        if (reply.fields.isEmpty()) reply.description="Unknown command. Use /help for the list.";
        return reply;
    },{{"command","Command to explain",RemoteOptionType::text,false,{},0,32,true},{"page","Page of commands",RemoteOptionType::integer,false,{},1,5}},[this](const QString&,const RemoteArguments& args) {
        RemoteChoices result;
        for (const auto& command:commands_) if (command.name.contains(args["command"].toString(),Qt::CaseInsensitive)) result.append({command.name,command.name});
        return result;
    }});
    bool changed=commands.size()!=commands_.size();
    for(int i=0;!changed && i<commands.size();++i)changed=remote_command_definition(commands[i])!=remote_command_definition(commands_[i]);
    commands_=std::move(commands);
    if(changed) { ++registration_revision_;cancel_commands();registered_=false;if(live_ && !guild_.isEmpty() && settings_.enabled)register_commands(); }
}
DiscordRemote::~DiscordRemote() { on_status={}; cancel(); }
void DiscordRemote::status(QString value) { if (status_==value) return; status_=std::move(value); if(on_status) on_status(status_); }
void DiscordRemote::cancel() {
    ++revision_; cancel_commands(); settings_.enabled=false; retry_.stop(); heartbeat_.stop(); deadline_.stop(); socket_.abort();
    const auto replies=replies_; for(auto* reply:replies) reply->abort();
    live_=registered_=awaiting_ack_=registration_active_=false; guild_.clear(); application_.clear(); session_.clear(); resume_url_=QUrl{}; sequence_=-1; seen_.clear();
}
bool DiscordRemote::configure(const RemoteSettings& settings) {
    if (const auto issue=validate(settings);!issue.isEmpty()) { status(issue); return false; }
    if (settings_==settings && !blocked_) return true;
    cancel(); settings_=settings; blocked_=false; failures_=0;
    if (settings_.enabled) { status("Connecting bot…"); open_gateway(); } else status("Off");
    return true;
}
void DiscordRemote::retry_connection() {
    if (!settings_.enabled) return;
    blocked_=false; failures_=0; retry_.stop();
    if (live_) bootstrap(); else { status("Connecting bot…"); open_gateway(); }
}
void DiscordRemote::fail(const QString& message) { blocked_=true; retry_.stop(); heartbeat_.stop(); deadline_.stop(); socket_.abort(); live_=false; status(message); }
void DiscordRemote::reconnect(int delay) {
    if (!settings_.enabled || blocked_ || retry_.isActive()) return;
    live_=false; heartbeat_.stop(); deadline_.stop();
    delay=std::max(delay,std::min(60000,5000*(1<<std::min(4u,failures_++))));
    status("Reconnecting…"); retry_.start(delay+int(QRandomGenerator::global()->bounded(500u)));
    socket_.abort();
}
void DiscordRemote::request(const QByteArray& method,const QString& path,const QJsonObject& data,bool authenticated,Response response,const QByteArray& image) {
    QNetworkRequest request(QUrl("https://discord.com/api/v10"+path));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(10000);
    if (authenticated) request.setRawHeader("Authorization","Bot "+settings_.token.toUtf8());
    QList<MessageFile> files;
    if (!image.isEmpty()) files.append({"game.jpg","image/jpeg","Roblox window",image});
    auto* reply=send_message(*network_,request,method,data,files);
    replies_.insert(reply); const auto revision=revision_;
    connect(reply,&QNetworkReply::finished,this,[this,reply,revision,response=std::move(response)] {
        replies_.remove(reply); reply->deleteLater();
        if (revision!=revision_ || !settings_.enabled || blocked_) return;
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto bytes=reply->read(65537); const auto body=bytes.size()<=65536?QJsonDocument::fromJson(bytes):QJsonDocument{};
        response(code,body);
    });
}
void DiscordRemote::bootstrap() {
    if (!live_) return;
    status("Bot online · checking channel…");
    guild_.clear(); registered_=false;
    request("GET","/channels/"+settings_.channel,{},true,[this](int code,const QJsonDocument& response) {
        const auto body=response.object();
        if (code==401) { fail("Bot token rejected. Update it in Remote control."); return; }
        if(code==403 && body["code"].toInt()==40333) {status("Bot online · Discord rejected the network request. Reconnect to retry.");return;}
        if (code==403 || code==404) { status("Bot online · cannot access this channel. Check the Channel ID, invite the bot to that server and allow View Channel, then reconnect."); return; }
        if (code!=200) {
            status("Bot online · channel check failed; retrying…");
            retry_.start(code==429?int(std::clamp(body["retry_after"].toDouble(5),1.,3600.)*1000):10000); return;
        }
        guild_=body["guild_id"].toString();
        if (!snowflake(guild_)) { guild_.clear();status("Bot online · choose a server channel, not a direct message."); return; }
        register_commands();
    });
}
void DiscordRemote::open_gateway() {
    if (!settings_.enabled || blocked_) return;
    retry_.stop(); awaiting_ack_=false; live_=false; deadline_.start(20000);
    socket_.open(resume_url_.isEmpty()?gateway():resume_url_);
}
void DiscordRemote::send(int opcode,const QJsonValue& data) {
    socket_.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"op",opcode},{"d",data}}).toJson(QJsonDocument::Compact)));
}
void DiscordRemote::heartbeat() { send(1,sequence_<0?QJsonValue(QJsonValue::Null):QJsonValue(sequence_)); awaiting_ack_=true; }
void DiscordRemote::receive(const QString& packet) {
    if (!settings_.enabled || blocked_ || packet.size()>1024*1024) return;
    QJsonParseError error; const auto document=QJsonDocument::fromJson(packet.toUtf8(),&error);
    if (error.error!=QJsonParseError::NoError || !document.isObject()) return;
    const auto event=document.object(); const int opcode=event["op"].toInt(-1); const auto data=event["d"].toObject();
    if (opcode==10) {
        heartbeat_ms_=data["heartbeat_interval"].toInt();
        if (heartbeat_ms_<1000 || heartbeat_ms_>300000) { fail("Discord sent an invalid heartbeat interval."); return; }
        heartbeat_.start(int(QRandomGenerator::global()->bounded(unsigned(heartbeat_ms_))));
        if (!session_.isEmpty() && sequence_>=0) send(6,QJsonObject{{"token",settings_.token},{"session_id",session_},{"seq",sequence_}});
        else send(2,QJsonObject{{"token",settings_.token},{"intents",0},{"properties",QJsonObject{{"os",QSysInfo::productType()},{"browser","Aurex"},{"device","Aurex"}}}});
    } else if (opcode==11) awaiting_ack_=false;
    else if (opcode==1) heartbeat();
    else if (opcode==7 || opcode==9) {
        if (opcode==9 && !event["d"].toBool()) { session_.clear(); resume_url_=QUrl{}; sequence_=-1; }
        socket_.abort(); reconnect();
    } else if (opcode==0) {
        if (event["s"].isDouble()) sequence_=event["s"].toInteger(-1);
        const auto name=event["t"].toString();
        if (name=="READY") {
            session_=data["session_id"].toString(); resume_url_=gateway(QUrl(data["resume_gateway_url"].toString()));
            application_=data["application"].toObject()["id"].toString();
            if (!snowflake(application_) || session_.isEmpty()) { fail("Discord did not identify the bot application."); return; }
            deadline_.stop(); failures_=0; live_=true; bootstrap();
        } else if (name=="RESUMED") {
            deadline_.stop(); failures_=0; live_=true;
            if (guild_.isEmpty()) bootstrap(); else if (!registered_) register_commands(); else status("Connected · "+settings_.device);
        } else if (name=="INTERACTION_CREATE" && live_) interaction(data);
    }
}
void DiscordRemote::retire_commands(QStringList names) {
    for(const auto& name:names) {
        if(!QRegularExpression("^[a-z][a-z0-9-]{0,31}$").match(name).hasMatch()
            || std::any_of(commands_.begin(),commands_.end(),[&](const auto& command){return command.name==name;}))
            throw std::invalid_argument("Cannot retire an active or invalid command");
    }
    retired_commands_=std::move(names);++registration_revision_;registered_=false;
    if(live_ && !guild_.isEmpty() && settings_.enabled)register_commands();
}
void DiscordRemote::register_commands(int index,QJsonArray existing) {
    if(registration_active_ || !live_ || guild_.isEmpty())return;
    const auto registration=registration_revision_;
    status(index<0?"Connected · checking commands…":QString("Connected · updating commands %1/%2…").arg(std::min(index+1,int(commands_.size()))).arg(commands_.size()));
    auto path="/applications/"+application_+"/guilds/"+guild_+"/commands";
    QJsonObject definition;
    int removal=-1;
    if (index>=0 && index<commands_.size()) {
        definition=remote_command_definition(commands_[index]);
        for (const auto& value:existing) {
            const auto old=value.toObject();
            if (old["type"].toInt(1)==1 && old["name"]==definition["name"] && old["description"]==definition["description"]
                && command_options(old["options"].toArray())==command_options(definition["options"].toArray())) {
                register_commands(index+1,std::move(existing)); return;
            }
        }
    }
    if(index>=commands_.size()) {
        for(int i=0;i<existing.size();++i) {
            const auto old=existing[i].toObject();const auto options=old["options"].toArray();
            if(old["type"].toInt(1)!=1 || !retired_commands_.contains(old["name"].toString()) || !snowflake(old["id"].toString()))continue;
            if(std::any_of(commands_.begin(),commands_.end(),[&](const auto& command){return command.name==old["name"].toString();}))continue;
            if(options.isEmpty() || options[0].toObject()["name"]!="device" || options[0].toObject()["type"]!=3 || !options[0].toObject()["required"].toBool())continue;
            removal=i;path+="/"+old["id"].toString();break;
        }
        if(removal<0) { registered_=true;status("Connected · "+settings_.device);return; }
        status("Connected · removing old commands…");
    }
    const auto method=index<0?"GET":removal>=0?"DELETE":"POST";
    registration_active_=true;
    request(method,path,definition,true,
        [this,index,registration,removal,existing=std::move(existing)](int code,const QJsonDocument& response) mutable {
        registration_active_=false;
        if (code==429) {
            const auto revision=revision_;registration_active_=true;
            const auto delay=int(std::ceil(std::clamp(response.object()["retry_after"].toDouble(5),1.,3600.)));
            status(QString("Connected · command sync resumes in %1s").arg(delay));
            QTimer::singleShot(delay*1000,this,
                [this,revision,registration,index,existing=std::move(existing)] {
                    if(revision!=revision_ || !settings_.enabled || blocked_) return;
                    registration_active_=false;
                    if(live_) register_commands(registration==registration_revision_?index:-1,existing);
                });
        } else if(registration!=registration_revision_) { if(live_ && settings_.enabled)register_commands(); }
        else if ((code>=200 && code<300) || (removal>=0 && code==404)) {
            if (index<0 && !response.isArray()) { fail("Discord returned an invalid command list."); return; }
            if(removal>=0)existing.removeAt(removal);
            register_commands(removal>=0?index:index+1,index<0?response.array():std::move(existing));
        } else if (code>=500 || !code) { socket_.abort(); reconnect(); }
        else status("Bot online · could not register commands. Invite it with applications.commands, then reconnect.");
    });
}
void DiscordRemote::respond(const QString& key,RemoteReply reply) {
    if (reply.deferred) {
        if (preparing_response_) { respond(key,{reply.title,"Another command is being prepared. Try again shortly."}); return; }
        preparing_response_=true;
        auto watcher=new QFutureWatcher<RemoteReply>(this); const auto revision=revision_;
        connect(watcher,&QFutureWatcher<RemoteReply>::finished,this,[this,watcher,revision,key] {
            preparing_response_=false; auto result=watcher->result(); watcher->deleteLater();
            if (revision==revision_ && settings_.enabled) respond(key,std::move(result));
        });
        watcher->setFuture(QtConcurrent::run([work=std::move(reply.deferred),title=reply.title] {
            try { return work(); }
            catch (const std::exception& error) { return RemoteReply{title,"Could not complete the command: "+QString::fromUtf8(error.what())}; }
            catch (...) { return RemoteReply{title,"Could not complete the command."}; }
        }));
        return;
    }
    if (!reply.image.isNull() && preparing_image_) {
        reply.image={}; reply.description="A screenshot is still being prepared. Try again shortly.";
    }
    QJsonObject content{{"title",redact(reply.title).left(256)},{"description",redact(reply.description).left(3500)},
        {"color",0x8abea1},{"footer",QJsonObject{{"text",settings_.device}}},
        {"timestamp",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    qsizetype remaining=5900-content["title"].toString().size()-content["description"].toString().size()-settings_.device.size();
    QJsonArray fields;
    for (const auto& [label,value]:reply.fields) {
        const auto name=redact(label).left(256);
        if (remaining<=name.size() || fields.size()>=25) break;
        const auto text=redact(value).left(std::min(qsizetype(1024),remaining-name.size()));
        if (name.isEmpty() || text.isEmpty()) continue;
        fields.append(QJsonObject{{"name",name},{"value",text},{"inline",reply.inline_fields}}); remaining-=name.size()+text.size();
    }
    content["fields"]=fields;
    const bool has_image=!reply.image.isNull();
    auto deliver=[this,key,has_image,content=std::move(content)](const QByteArray& image) mutable {
        if (!image.isEmpty()) content["image"]=QJsonObject{{"url","attachment://game.jpg"}};
        else if (has_image) content["description"]="Could not prepare the screenshot.";
        auto body=message({}); body["embeds"]=QJsonArray{content};
        request("PATCH","/webhooks/"+application_+"/"+key+"/messages/@original",body,false,
            [this](int code,const QJsonDocument&) { if (code!=200) status("Command handled; Discord could not receive its response."); },image);
    };
    if (reply.image.isNull()) { deliver({}); return; }
    preparing_image_=true;
    auto watcher=new QFutureWatcher<QByteArray>(this); const auto revision=revision_;
    connect(watcher,&QFutureWatcher<QByteArray>::finished,this,[this,watcher,revision,deliver=std::move(deliver)]() mutable {
        watcher->deleteLater(); preparing_image_=false;
        if (revision!=revision_ || !settings_.enabled) return;
        deliver(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([image=std::move(reply.image)] {
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        auto scaled=image.scaled(1280,720,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        if (!scaled.save(&buffer,"JPG",80) || bytes.size()>1024*1024) return QByteArray{};
        return bytes;
    }));
}
void DiscordRemote::interaction(const QJsonObject& event) {
    const bool autocomplete=event["type"].toInt()==4;
    if ((!autocomplete && event["type"].toInt()!=2) || event["application_id"].toString()!=application_) return;
    const auto data=event["data"].toObject(); const auto options=data["options"].toArray();
    const auto found=std::find_if(commands_.begin(),commands_.end(),[&](const auto& command) { return command.name==data["name"].toString(); });
    RemoteArguments args; QString focused,issue;
    for (const auto& value:options) {
        const auto option=value.toObject(); const auto name=option["name"].toString();
        if (args.contains(name)) { issue="Duplicate argument."; break; }
        args.insert(name,option["value"]);
        if (option["focused"].toBool()) focused=name;
        if (name=="device") {
            if (option["type"].toInt()!=3 || !option["value"].isString()) issue="Invalid device name.";
            continue;
        }
        if (found==commands_.end()) continue;
        const auto expected=std::find_if(found->options.begin(),found->options.end(),[&](const auto& field) { return field.name==name; });
        if (expected==found->options.end() || option["type"].toInt()!=int(expected->type)) { issue="Unknown argument or wrong type."; continue; }
        const auto input=option["value"];
        if (expected->type==RemoteOptionType::text) {
            const auto text=input.toString();
            if (!input.isString() || (!autocomplete && (text.size()<expected->minimum || text.size()>expected->maximum
                || (!expected->choices.isEmpty() && !expected->choices.contains(text))))) issue="Invalid value for "+name+".";
        } else if (expected->type==RemoteOptionType::integer || expected->type==RemoteOptionType::number) {
            const auto number=input.toDouble();
            if (!input.isDouble() || !std::isfinite(number) || (expected->type==RemoteOptionType::integer && std::floor(number)!=number) || number<expected->minimum || number>expected->maximum) issue="Invalid number for "+name+".";
        } else if (!input.isBool()) issue="Invalid switch for "+name+".";
    }
    if (!(autocomplete && focused=="device") && args["device"].toString()!=settings_.device) return;
    const auto id=event["id"].toString(),key=event["token"].toString();
    if (!snowflake(id) || !token(key) || seen_.contains(id)) return;
    seen_.append(id); if (seen_.size()>128) seen_.removeFirst();
    const auto user=event["member"].toObject()["user"].toObject();
    const bool allowed=event["guild_id"].toString()==guild_ && event["channel_id"].toString()==settings_.channel
        && user["id"].toString()==settings_.user && !user["bot"].toBool();
    const auto callback="/interactions/"+id+"/"+key+"/callback";
    if (autocomplete) {
        RemoteChoices matches;
        if (allowed && issue.isEmpty()) try {
            if (focused=="device" && settings_.device.contains(args["device"].toString(),Qt::CaseInsensitive)) matches.append({settings_.device,settings_.device});
            else if (found!=commands_.end() && found->complete) matches=found->complete(focused,args);
        } catch (...) {}
        QJsonArray choices;
        for (const auto& [name,value]:matches) {
            if (choices.size()==25) break;
            if (!name.isEmpty() && !value.isEmpty() && value.size()<=100) choices.append(QJsonObject{{"name",name.left(100)},{"value",value}});
        }
        request("POST",callback,{{"type",8},{"data",QJsonObject{{"choices",choices}}}},false,[](int,const QJsonDocument&) {}); return;
    }
    if (found==commands_.end()) issue="Command unavailable on this device. Enable its module or use /help for available commands.";
    else for (const auto& option:found->options) if (option.required && !args.contains(option.name)) issue="Missing "+option.name+".";
    if (!allowed || !issue.isEmpty()) {
        auto body=message(!allowed?QString("This user or channel is not allowed to control this device."):issue);
        if(!allowed)body["flags"]=64;
        request("POST",callback,{{"type",4},{"data",body}},false,[](int,const QJsonDocument&) {}); return;
    }
    args.remove("device");
    const auto order=found->control?++command_revision_:command_revision_;
    request("POST",callback,{{"type",5}},false,
        [this,key,command=*found,args,order,catalog=registration_revision_,expires=QDeadlineTimer(3000)](int code,const QJsonDocument&) {
        if (code!=204 && code!=200) return;
        RemoteReply result{command.name};
        try {
            if (expires.hasExpired()) result.description="Command expired before it could run. Please try again.";
            else if (catalog!=registration_revision_) result.description="Command changed or became unavailable. Please try again.";
            else if (command.control && order!=command_revision_) result.description="Cancelled by a newer command.";
            else result=command.run(args);
        }
        catch (const std::exception& error) { result.description="Could not complete the command: "+QString::fromUtf8(error.what()); }
        catch (...) { result.description="Could not complete the command."; }
        respond(key,std::move(result));
    });
}
}
