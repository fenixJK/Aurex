#include "controller.hpp"
#include <QJsonDocument>
#include "game/data/join.hpp"
#include <QUrl>
#include <QUuid>

namespace hive::features::accounts {
Controller::Controller(Credentials credentials,coordination::RouterMapping router,std::shared_ptr<coordination::State> state)
    :credentials_(std::move(credentials)),state_(state?std::move(state):throw std::invalid_argument("Alt Sync needs connection state.")),connection_(state_,nullptr,std::move(router)),discord_(state_),name_("My account") {
    if(!credentials_.read || !credentials_.write) throw std::invalid_argument("Alt Sync needs credential storage.");
    connection_.on_changed=[this] { changed(); };
    discord_.on_changed=[this] { changed(); };
    discord_.on_message=[this](const QString& message) {
        try { credentials_.write("discord-message",message); }
        catch (...) { issue_="Connected, but the Discord message could not be saved for next time.";changed(); }
    };
}
Controller::~Controller() { on_changed={};connection_.on_changed={};discord_.on_changed={};discord_.on_message={};disable(); }
void Controller::changed() { if(on_changed) on_changed(); }
QString Controller::status() const { return !enabled_?"Enable Alt Sync to connect accounts.":!issue_.isEmpty()?issue_:!joined_?"No main connected · using this account's Gathering settings":(discord_mode_?discord_.status():connection_.status()); }
void Controller::enable() {
    if(enabled_) return;
    enabled_=true;issue_.clear();
    try {
        account_id_=credentials_.read("account-id");
        if (QUuid(account_id_).isNull()) {
            account_id_=QUuid::createUuid().toString(QUuid::WithoutBraces);
            credentials_.write("account-id",account_id_);
        }
        const auto saved=credentials_.read("group");
        if(!saved.isEmpty()) {
            auto config=QJsonDocument::fromJson(saved.toUtf8()).object();
            if(config["mode"]=="host") {
                auto identity=coordination::load_identity(config["identity"].toString());
                identity.key=credentials_.read("host-key").toLatin1();config["identity"]=coordination::save_identity(identity);
            }
            activate(std::move(config),false);
        }
    } catch(const std::exception& error) { joined_=true;issue_=QString::fromUtf8(error.what()); }
    changed();
}
void Controller::disable() noexcept {
    main_boost_->update({});
    role_=coordination::Role::offline;
    enabled_=false;joined_=false;state_->clear_publication();connection_.leave();discord_.leave();issue_.clear();server_.clear();epoch_.clear();changed();
}
void Controller::activate(QJsonObject config,bool save) {
    if(!enabled_) throw std::logic_error("Enable Alt Sync before connecting.");
    const auto mode=config["mode"].toString(),name=config["name"].toString();
    QString server;
    const bool via_discord=config["network"].toString()=="discord" || config["invite"].toString().startsWith("aurex://discord/");
    if(via_discord) {
        coordination::DiscordInvite group;
        if(mode=="host") {
            server=config["server"].toString();
            if(!server.isEmpty()) game::JoinTarget::from_uri(server.toStdString());
            group={config["channel"].toString(),coordination::load_identity(config["identity"].toString())};
        } else if(mode=="join") group=coordination::decode_discord_invite(config["invite"].toString());
        else throw std::invalid_argument("Invalid saved Alt Sync group.");
        const auto token=save?config["bot-token"].toString():credentials_.read("discord-token");
        const auto id=config["peer-id"].toString();
        if(id.isEmpty()) throw std::invalid_argument("Invalid saved Alt Sync account identity.");
        discord_.connect_group(group,name,id,token,save?QString{}:credentials_.read("discord-message"));
        connection_.leave();discord_mode_=true;
    } else if(mode=="host") {
        server=config["server"].toString();
        if(!server.isEmpty()) game::JoinTarget::from_uri(server.toStdString());
        const auto port=config["port"].toInt();if(port<1 || port>65535) throw std::invalid_argument("Invalid host port.");
        const auto identity=coordination::load_identity(config["identity"].toString());
        const auto network=config["network"].toString("internet");
        if(network=="local") connection_.host_local(identity,name,config["interface"].toString(),quint16(port));
        else if(network=="internet") connection_.host(identity,name,config["endpoint"].toString(),QHostAddress::Any,quint16(port));
        else throw std::invalid_argument("Saved network mode is invalid. Leave the group and host again.");
    } else if(mode=="join") connection_.join(coordination::decode_invite(config["invite"].toString()),name);
    else throw std::invalid_argument("Saved group is invalid. Leave the group and join again.");
    if(!via_discord) { discord_.leave();discord_mode_=false; }
    try {
        if(save) {
            auto identity=coordination::load_identity(config["identity"].toString());
            credentials_.write("host-key",QString::fromLatin1(identity.key));identity.key.clear();
            if(mode=="host") config["identity"]=coordination::save_identity(identity);
            if(via_discord) credentials_.write("discord-token",config.take("bot-token").toString());
            else credentials_.write("discord-token",{});
            credentials_.write("discord-message",{});
            credentials_.write("group",QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)));
        }
    } catch(...) { connection_.leave();discord_.leave();throw; }
    main_boost_->update({});
    role_=mode=="host"?coordination::Role::host:coordination::Role::guest;
    name_=name;server_=server;epoch_=QUuid::createUuid().toString(QUuid::WithoutBraces);joined_=true;issue_.clear();changed();
}
void Controller::host(QString name,QString address,quint16 port,QString server,HostNetwork network,QString network_id) {
    if(!enabled_) throw std::logic_error("Enable Alt Sync before connecting.");
    QJsonObject config{{"mode","host"},{"name",name.trimmed()},{"port",port}};
    config["network"]=network==HostNetwork::local?"local":"internet";
    if(network==HostNetwork::local && !network_id.isEmpty()) config["interface"]=network_id;
    address=address.trimmed();
    if(network==HostNetwork::internet && !address.isEmpty()) {
        if(!address.startsWith("wss://")) address="wss://"+address;
        QUrl endpoint(address);if(endpoint.port()<0) endpoint.setPort(port);
        const auto issue=coordination::validate_endpoint(endpoint.toString());
        if(!issue.isEmpty()) throw std::invalid_argument(issue.toStdString());
        config["endpoint"]=endpoint.toString();
    }
    server=server.trimmed();
    if(!server.isEmpty()) {
        const auto target=server.startsWith("https://")?game::JoinTarget(server.toStdString()):game::JoinTarget::follow_user(server.toStdString());
        config["server"]=QString::fromStdString(target.uri());
    }
    config["identity"]=coordination::save_identity(coordination::create_identity());activate(std::move(config),true);
}
void Controller::join(QString name,QString invite,QString discord_token) {
    QJsonObject config{{"mode","join"},{"name",name.trimmed()},{"invite",invite.trimmed()}};
    if(invite.trimmed().startsWith("aurex://discord/")) {
        config["bot-token"]=discord_token;config["peer-id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    activate(std::move(config),true);
}
void Controller::host_discord(QString name,QString channel,QString bot_token,QString server) {
    QJsonObject config{{"mode","host"},{"network","discord"},{"name",name.trimmed()},{"channel",channel.trimmed()},
        {"bot-token",bot_token},{"peer-id",QUuid::createUuid().toString(QUuid::WithoutBraces)}};
    server=server.trimmed();
    if(!server.isEmpty()) config["server"]=QString::fromStdString((server.startsWith("https://")?game::JoinTarget(server.toStdString()):game::JoinTarget::follow_user(server.toStdString())).uri());
    config["identity"]=coordination::save_identity(coordination::create_identity());activate(std::move(config),true);
}
void Controller::leave() {
    main_boost_->update({});
    role_=coordination::Role::offline;
    state_->clear_publication();connection_.leave();discord_.leave();joined_=false;issue_.clear();server_.clear();epoch_.clear();
    try { credentials_.write("group",{});credentials_.write("host-key",{});credentials_.write("discord-token",{});credentials_.write("discord-message",{}); }
    catch(const std::exception& error) { issue_=QString::fromUtf8(error.what()); }
    changed();
}
void Controller::retry() { issue_.clear();connection_.retry_mapping();changed(); }
}
