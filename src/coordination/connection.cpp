#include "connection.hpp"
#include "local_network.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QTimer>
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketServer>
#include <limits>
#include <algorithm>
#include <set>
#include <QSslSocket>
#include <stdexcept>

namespace hive::coordination {
namespace {
constexpr int maximum_peers=16,maximum_state=8192;
constexpr auto heartbeat=std::chrono::seconds(5);
QString fingerprint(const QSslCertificate& certificate) { return QString::fromLatin1(certificate.digest(QCryptographicHash::Sha256).toHex()); }
bool hex_key(const QString& value) { static const QRegularExpression pattern("^[a-f0-9]{64}$");return pattern.match(value).hasMatch(); }
bool label(const QString& value,int maximum=128) {
    return !value.isEmpty() && value.size()<=maximum && std::none_of(value.begin(),value.end(),[](QChar c) { return c.category()==QChar::Other_Control; });
}
void require(bool ok,const char* message) { if(!ok) throw std::invalid_argument(message); }
QJsonObject encode_topics(const Topics& topics) {
    QJsonObject result;
    for(const auto& [topic,values]:topics) {
        QJsonObject data;
        for(const auto& [key,value]:values) std::visit([&](const auto& item) {
            using T=std::decay_t<decltype(item)>;
            if constexpr(std::is_same_v<T,std::string>) data[QString::fromStdString(key)]=QString::fromStdString(item);
            else if constexpr(std::is_same_v<T,SettingSelection>) { QJsonArray array;for(const auto& s:item) array.append(QString::fromStdString(s));data[QString::fromStdString(key)]=array; }
            else data[QString::fromStdString(key)]=item;
        },value);
        result[QString::fromStdString(topic)]=data;
    }
    return result;
}
Topics decode_topics(const QJsonValue& value) {
    require(value.isObject(),"Invalid account state");const auto object=value.toObject();
    require(object.size()<=16 && QJsonDocument(object).toJson(QJsonDocument::Compact).size()<=maximum_state,"Account state is too large");
    Topics result;
    for(auto topic=object.begin();topic!=object.end();++topic) {
        require(label(topic.key(),64) && topic.value().isObject(),"Invalid account topic");
        const auto data=topic.value().toObject();require(data.size()<=16,"Too many account values");Settings values;
        for(auto item=data.begin();item!=data.end();++item) {
            require(label(item.key(),64),"Invalid account key");const auto v=item.value();SettingValue entry;
            if(v.isBool()) entry=v.toBool();
            else if(v.isDouble()) { const auto number=v.toDouble();require(number>=std::numeric_limits<int>::min() && number<=std::numeric_limits<int>::max() && number==int(number),"Invalid account number");entry=int(number); }
            else if(v.isString()) { require(v.toString().isEmpty() || label(v.toString()),"Invalid account text");entry=v.toString().toStdString(); }
            else if(v.isArray()) {
                require(v.toArray().size()<=16,"Too many account choices");SettingSelection choices;
                for(const auto& choice:v.toArray()) { require(choice.isString() && label(choice.toString()),"Invalid account choice");choices.push_back(choice.toString().toStdString()); }entry=std::move(choices);
            } else throw std::invalid_argument("Invalid account value");
            values.emplace(item.key().toStdString(),std::move(entry));
        }
        result.emplace(topic.key().toStdString(),std::move(values));
    }
    return result;
}
void send(QWebSocket* socket,const QJsonObject& value) {
    if(socket->bytesToWrite()>256*1024) { QMetaObject::invokeMethod(socket,[socket] { socket->abort(); },Qt::QueuedConnection);return; }
    socket->sendTextMessage(QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)));
}
void limit(QWebSocket* socket,quint64 size) {
    socket->setMaxAllowedIncomingFrameSize(size);socket->setMaxAllowedIncomingMessageSize(size);
}
bool same_token(const QString& left,const QString& right) {
    if(left.size()!=64 || right.size()!=64) return false;
    unsigned difference=0;for(int i=0;i<64;++i) difference|=left[i].unicode()^right[i].unicode();return difference==0;
}
}
QString validate_endpoint(const QString& value) {
    const QUrl url(value,QUrl::StrictMode);
    if(value.size()>512 || !url.isValid() || url.scheme()!="wss" || url.host().isEmpty() || !url.userInfo().isEmpty()
        || url.hasQuery() || url.hasFragment() || (url.path()!="/" && !url.path().isEmpty()) || url.port(45831)<1)
        return "Enter a host address such as wss://your-address:45831.";
    return {};
}
QString encode_invite(const Invite& value) {
    require(validate_endpoint(value.endpoint).isEmpty() && hex_key(value.fingerprint) && hex_key(value.token),"Invalid group invite");
    const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"endpoint",value.endpoint},{"fingerprint",value.fingerprint},{"token",value.token}}).toJson(QJsonDocument::Compact);
    return "aurex://join/"+QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals));
}
Invite decode_invite(const QString& value) {
    require(value.startsWith("aurex://join/") && value.size()<=2048,"Paste an Aurex group invite.");
    const auto bytes=QByteArray::fromBase64(value.mid(13).toLatin1(),QByteArray::Base64UrlEncoding|QByteArray::AbortOnBase64DecodingErrors);
    const auto data=QJsonDocument::fromJson(bytes).object();require(data["version"].toInt()==1,"This group invite is not supported.");
    Invite result{data["endpoint"].toString(),data["fingerprint"].toString(),data["token"].toString()};encode_invite(result);return result;
}
struct Connection::Impl {
    Connection& owner;
    std::shared_ptr<State> state;
    std::unique_ptr<QWebSocketServer> server;
    std::unique_ptr<QWebSocket> client;
    struct Member { Peer peer;bool authenticated{};Clock::time_point seen=Clock::now(),window=Clock::now();int messages{}; };
    std::map<QWebSocket*,Member> members;
    QTimer timer;
    QString name,message="Not connected",token;
    Invite invite,host_invite;
    PortMapper mapper;
    bool automatic_host{};
    QString mapped_endpoint;
    bool enabled{},welcomed{};
    std::uint64_t sent_revision=std::numeric_limits<std::uint64_t>::max();
    Clock::time_point last_heartbeat{},received{},retry_at{};
    int retry_seconds=1;
    Impl(Connection& value,std::shared_ptr<State> shared,RouterMapping router):owner(value),state(std::move(shared)),mapper(std::move(router)) {
        mapper.on_changed=[this] {
            if(!server || !automatic_host) return;
            const auto mapping=mapper.status();
            if(mapping.phase==MappingPhase::ready) {
                const auto endpoint=mapping.mapping.endpoint();
                status(!mapped_endpoint.isEmpty() && mapped_endpoint!=endpoint?"Host address changed · copy a new invite":mapping.message);
                mapped_endpoint=endpoint;
            } else { mapped_endpoint.clear();status(mapping.message); }
        };
        timer.setInterval(250);QObject::connect(&timer,&QTimer::timeout,&owner,[this] { tick(); });
    }
    bool status(QString value) { if(message==value) return false;message=std::move(value);changed();return true; }
    void changed() { if(owner.on_changed) owner.on_changed(); }
    void disconnect_client() {
        if(client) { QObject::disconnect(client.get(),nullptr,&owner,nullptr);client->abort();client.reset(); }
        welcomed=false;state->disconnect();
    }
    void leave() {
        automatic_host=false;mapper.stop();mapped_endpoint.clear();host_invite={};
        enabled=false;timer.stop();disconnect_client();
        for(auto& [socket,member]:members) { QObject::disconnect(socket,nullptr,&owner,nullptr);socket->abort();socket->deleteLater(); }
        members.clear();server.reset();sent_revision=std::numeric_limits<std::uint64_t>::max();status("Not connected");changed();
    }
    Snapshot host_snapshot() {
        Snapshot snapshot{Role::host,{{"host",name.toStdString(),true,state->publication().topics}}};
        for(const auto& [socket,member]:members) if(member.authenticated) snapshot.peers.push_back(member.peer);
        return snapshot;
    }
    void broadcast() {
        auto snapshot=host_snapshot();QJsonArray peers;
        try {
            for(const auto& peer:snapshot.peers) { const auto topics=encode_topics(peer.topics);decode_topics(topics);
                peers.append(QJsonObject{{"id",QString::fromStdString(peer.id)},{"name",QString::fromStdString(peer.name)},{"host",peer.host},{"topics",topics}}); }
        } catch(const std::exception&) { status("An account module supplied invalid shared data.");return; }
        const bool updated=state->receive(std::move(snapshot));const QJsonObject packet{{"kind","snapshot"},{"peers",peers}};
        for(const auto& [socket,member]:members) if(member.authenticated) send(socket,packet);
        if(updated) changed();
    }
    void accept() {
        while(server && server->hasPendingConnections()) {
            auto socket=server->nextPendingConnection();socket->setParent(&owner);limit(socket,16384);
            if(members.size()>=maximum_peers-1) { socket->close(QWebSocketProtocol::CloseCodePolicyViolated,"Group full");socket->deleteLater();continue; }
            members.emplace(socket,Member{});
            QObject::connect(socket,&QWebSocket::disconnected,&owner,[this,socket] { if(members.erase(socket) && server) broadcast();socket->deleteLater(); });
            QObject::connect(socket,&QWebSocket::pong,&owner,[this,socket] { if(auto i=members.find(socket);i!=members.end()) i->second.seen=Clock::now(); });
            QObject::connect(socket,&QWebSocket::binaryMessageReceived,&owner,[socket] { socket->close(QWebSocketProtocol::CloseCodePolicyViolated,"Text protocol required"); });
            QObject::connect(socket,&QWebSocket::textMessageReceived,&owner,[this,socket](const QString& text) {
                auto found=members.find(socket);if(found==members.end()) return;
                try {
                    auto& member=found->second;const auto now=Clock::now();
                    if(now-member.window>=std::chrono::seconds(1)) { member.window=now;member.messages=0; }
                    require(++member.messages<=20,"Too many messages");
                    const auto packet=QJsonDocument::fromJson(text.toUtf8()).object();
                    if(!member.authenticated) {
                        require(packet["kind"]=="hello" && packet["version"].toInt()==1 && same_token(packet["token"].toString(),token),"Invite rejected");
                        require(label(packet["name"].toString(),32),"Invalid account name");
                        member.peer={QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),packet["name"].toString().toStdString(),false,{}};
                        member.authenticated=true;member.seen=now;broadcast();
                    } else {
                        require(packet["kind"]=="state","Unknown account message");
                        auto topics=decode_topics(packet["topics"]);member.seen=now;
                        if(member.peer.topics!=topics) { member.peer.topics=std::move(topics);broadcast(); }
                    }
                } catch(const std::exception& error) { socket->close(QWebSocketProtocol::CloseCodePolicyViolated,QString::fromUtf8(error.what())); }
            });
        }
    }
    bool pinned() const { return client && fingerprint(client->sslConfiguration().peerCertificate())==invite.fingerprint; }
    void connect_client() {
        disconnect_client();received=Clock::now();sent_revision=std::numeric_limits<std::uint64_t>::max();
        client=std::make_unique<QWebSocket>();client->setProxy(QNetworkProxy::NoProxy);limit(client.get(),256*1024);
        auto ssl=QSslConfiguration::defaultConfiguration();ssl.setProtocol(QSsl::TlsV1_2OrLater);client->setSslConfiguration(ssl);
        QObject::connect(client.get(),&QWebSocket::sslErrors,&owner,[this](const QList<QSslError>& errors) {
            if(!pinned()) { status("Host identity changed. Ask for a new invite.");client->abort();return; }
            for(const auto& error:errors) if(error.error()!=QSslError::SelfSignedCertificate && error.error()!=QSslError::HostNameMismatch && error.error()!=QSslError::CertificateUntrusted) {
                status("The host's secure connection could not be verified.");client->abort();return;
            }
            client->ignoreSslErrors(errors);
        });
        QObject::connect(client.get(),&QWebSocket::connected,&owner,[this] {
            if(!pinned()) { status("Host identity does not match the invite.");client->abort();return; }
            send(client.get(),{{"kind","hello"},{"version",1},{"name",name},{"token",invite.token}});
        });
        QObject::connect(client.get(),&QWebSocket::disconnected,&owner,[this] {
            welcomed=false;state->disconnect();
            if(client->closeCode()==QWebSocketProtocol::CloseCodePolicyViolated) { enabled=false;status("The host rejected this invite or account. Ask for a new invite.");timer.stop(); }
            else { retry_at=Clock::now()+std::chrono::seconds(retry_seconds);retry_seconds=std::min(30,retry_seconds*2);status("Host disconnected · continuing locally"); }
            changed();
        });
        QObject::connect(client.get(),&QWebSocket::errorOccurred,&owner,[this] {
            retry_at=Clock::now()+std::chrono::seconds(retry_seconds);retry_seconds=std::min(30,retry_seconds*2);
            status("Connection failed: "+client->errorString()+" · retrying");
        });
        QObject::connect(client.get(),&QWebSocket::textMessageReceived,&owner,[this](const QString& text) {
            try {
                const auto packet=QJsonDocument::fromJson(text.toUtf8()).object();require(packet["kind"]=="snapshot" && packet["peers"].isArray(),"Invalid group state");
                const auto peers=packet["peers"].toArray();require(!peers.isEmpty() && peers.size()<=maximum_peers,"Invalid group size");
                Snapshot snapshot{Role::guest,{}};std::set<QString> ids;int hosts=0;
                for(const auto& value:peers) {
                    const auto p=value.toObject();const auto id=p["id"].toString();
                    require(label(id,40) && ids.insert(id).second && label(p["name"].toString(),32) && p["host"].isBool(),"Invalid group member");
                    hosts+=p["host"].toBool();snapshot.peers.push_back({id.toStdString(),p["name"].toString().toStdString(),p["host"].toBool(),decode_topics(p["topics"])});
                }
                require(hosts==1,"Invalid group host");const bool updated=state->receive(std::move(snapshot));received=Clock::now();welcomed=true;retry_seconds=1;
                if(!status("Connected to host") && updated) changed();
            } catch(const std::exception&) { client->close(QWebSocketProtocol::CloseCodePolicyViolated,"Invalid group state"); }
        });
        QUrl endpoint(invite.endpoint);if(endpoint.port()<0) endpoint.setPort(45831);
        status("Connecting to host…");client->open(endpoint);
    }
    void tick() {
        const auto now=Clock::now();
        if(server) {
            std::vector<QWebSocket*> expired;
            for(const auto& [socket,member]:members) if(now-member.seen>(member.authenticated?peer_expiry:heartbeat)) expired.push_back(socket);
            for(auto* socket:expired) socket->abort();
            const auto revision=state->publication_revision();
            if(revision!=sent_revision || now-last_heartbeat>=heartbeat) {
                sent_revision=revision;last_heartbeat=now;
                for(const auto& [socket,member]:members) if(member.authenticated) socket->ping();
                broadcast();
            }
        } else if(enabled) {
            if(!client || (client->state()==QAbstractSocket::UnconnectedState && now>=retry_at)) { connect_client();return; }
            if(now-received>=peer_expiry && client->state()!=QAbstractSocket::UnconnectedState) { client->abort();state->disconnect();return; }
            if(welcomed && state->publication_revision()!=sent_revision) {
                const auto local=state->publication();
                const auto topics=encode_topics(local.topics);
                try { decode_topics(topics);send(client.get(),{{"kind","state"},{"topics",topics}});sent_revision=local.revision; }
                catch(const std::exception&) { status("An account module supplied invalid shared data."); }
            }
        }
    }
};
Connection::Connection(std::shared_ptr<State> state,QObject* parent,RouterMapping router):QObject(parent),impl_(std::make_unique<Impl>(*this,std::move(state),std::move(router))) {}
Connection::~Connection() { on_changed={};impl_->leave(); }
QString Connection::host(const HostIdentity& identity,const QString& name,const QString& endpoint,const QHostAddress& bind,quint16 port) {
    require(label(name,32),"Enter an account name (up to 32 characters).");require(endpoint.isEmpty() || validate_endpoint(endpoint).isEmpty(),"Enter a reachable host address.");
    const QSslCertificate certificate(identity.certificate);const QSslKey key(identity.key,QSsl::Rsa);
    require(!certificate.isNull() && !key.isNull() && hex_key(identity.token),"The saved host identity is invalid.");
    auto server=std::make_unique<QWebSocketServer>("Aurex accounts",QWebSocketServer::SecureMode);
    auto ssl=QSslConfiguration::defaultConfiguration();ssl.setProtocol(QSsl::TlsV1_2OrLater);ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    ssl.setLocalCertificate(certificate);ssl.setPrivateKey(key);server->setSslConfiguration(ssl);server->setHandshakeTimeout(5000);server->setMaxPendingConnections(maximum_peers);
    require(server->listen(bind,port),"Could not host on this port. Choose another port or close the other host.");
    QUrl address(endpoint);if(!port && !endpoint.isEmpty()) address.setPort(server->serverPort());
    const Invite details{address.toString(),fingerprint(certificate),identity.token};
    impl_->leave();impl_->name=name;impl_->token=identity.token;impl_->server=std::move(server);
    connect(impl_->server.get(),&QWebSocketServer::newConnection,this,[this] { impl_->accept(); });
    impl_->host_invite=details;impl_->automatic_host=endpoint.isEmpty();
    impl_->timer.start();
    if(impl_->automatic_host) impl_->mapper.start(impl_->server->serverPort());
    else impl_->status("Hosting · share an invite");
    impl_->broadcast();return invite();
}
QString Connection::host_local(const HostIdentity& identity,const QString& name,const QString& network,quint16 port) {
    const auto local=local_network(network);
    QUrl endpoint;endpoint.setScheme("wss");endpoint.setHost(local.address.toString());if(port) endpoint.setPort(port);
    const auto code=host(identity,name,endpoint.toString(),local.address,port);
    impl_->status("Hosting on this network · share an invite");return code;
}
void Connection::join(const Invite& invite,const QString& name) {
    encode_invite(invite);require(label(name,32),"Enter an account name (up to 32 characters).");
    impl_->leave();impl_->invite=invite;impl_->name=name;impl_->enabled=true;impl_->retry_seconds=1;impl_->connect_client();impl_->timer.start();
}
void Connection::leave() { impl_->leave(); }
QString Connection::status() const { return impl_->message; }
bool Connection::hosting() const { return bool(impl_->server); }
QString Connection::invite() const {
    if(!impl_->server) return {};
    auto value=impl_->host_invite;
    if(impl_->automatic_host) {
        const auto mapping=impl_->mapper.status();if(mapping.phase!=MappingPhase::ready) return {};
        value.endpoint=mapping.mapping.endpoint();
    }
    return encode_invite(value);
}
bool Connection::mapping_failed() const { return hosting() && impl_->automatic_host && impl_->mapper.status().phase==MappingPhase::unavailable; }
void Connection::retry_mapping() { if(mapping_failed()) impl_->mapper.start(impl_->server->serverPort()); }
}
