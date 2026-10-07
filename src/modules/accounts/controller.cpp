#include "hive/modules/accounts/controller.hpp"
#include <QJsonDocument>
#include "hive/game/data/join.hpp"
#include <QSysInfo>
#include <QUrl>
#include <QUuid>

namespace hive::features::accounts {
Controller::Controller(Credentials credentials,coordination::RouterMapping router,std::shared_ptr<coordination::State> state)
    :credentials_(std::move(credentials)),state_(state?std::move(state):throw std::invalid_argument("Accounts needs connection state.")),connection_(state_,nullptr,std::move(router)),name_(QSysInfo::machineHostName().left(32)) {
    if(!credentials_.read || !credentials_.write) throw std::invalid_argument("Accounts needs credential storage.");
    connection_.on_changed=[this] { changed(); };
}
Controller::~Controller() { on_changed={};connection_.on_changed={};disable(); }
void Controller::changed() { if(on_changed) on_changed(); }
QString Controller::status() const { return !enabled_?"Enable Accounts to host or join a group.":!issue_.isEmpty()?issue_:connection_.status(); }
void Controller::enable() {
    if(enabled_) return;
    enabled_=true;issue_.clear();
    try {
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
    enabled_=false;joined_=false;state_->clear_publication();connection_.leave();issue_.clear();server_.clear();changed();
}
void Controller::activate(QJsonObject config,bool save) {
    if(!enabled_) throw std::logic_error("Enable Accounts before connecting.");
    const auto mode=config["mode"].toString(),name=config["name"].toString();
    QString server;
    if(mode=="host") {
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
    try {
        if(save) {
            auto identity=coordination::load_identity(config["identity"].toString());
            credentials_.write("host-key",QString::fromLatin1(identity.key));identity.key.clear();
            if(mode=="host") config["identity"]=coordination::save_identity(identity);
            credentials_.write("group",QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)));
        }
    } catch(...) { connection_.leave();throw; }
    name_=name;server_=server;epoch_=QUuid::createUuid().toString(QUuid::WithoutBraces);joined_=true;issue_.clear();changed();
}
void Controller::host(QString name,QString address,quint16 port,QString server,HostNetwork network,QString network_id) {
    if(!enabled_) throw std::logic_error("Enable Accounts before connecting.");
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
void Controller::join(QString name,QString invite) { activate({{"mode","join"},{"name",name.trimmed()},{"invite",invite.trimmed()}},true); }
void Controller::leave() {
    state_->clear_publication();connection_.leave();joined_=false;issue_.clear();server_.clear();
    try { credentials_.write("group",{});credentials_.write("host-key",{}); }
    catch(const std::exception& error) { issue_=QString::fromUtf8(error.what()); }
    changed();
}
void Controller::retry() { issue_.clear();connection_.retry_mapping();changed(); }
}
