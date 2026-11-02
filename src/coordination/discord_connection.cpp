#include "discord_connection.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace hive::coordination {
namespace {
constexpr auto lifetime=std::chrono::seconds(15);
constexpr auto heartbeat=std::chrono::seconds(5);
constexpr auto poll_interval=std::chrono::seconds(3);
constexpr qint64 max_response=1024*1024;
std::chrono::milliseconds delay(double seconds) {
    return std::chrono::milliseconds(qint64(std::ceil(std::clamp(std::isfinite(seconds)?seconds:60.,1.,86400.)*1000)));
}
}
struct DiscordConnection::Impl {
    DiscordConnection& owner;
    std::shared_ptr<State> state;
    QNetworkAccessManager* network;
    QPointer<QNetworkReply> pending;
    QTimer timer;
    DiscordInvite group;
    Peer self;
    QString token,bot_id,message_id,create_nonce,encoded_invite,message="Not connected";
    std::map<std::string,std::pair<DiscordPacket,Clock::time_point>> peers;
    std::map<std::string,qint64> latest;
    bool enabled{},halted{},read_next{true},hosting{};
    Clock::time_point read_at{},write_at{},blocked_until{};
    std::uint64_t generation{};
    qint64 server_offset{},last_sent{};
    int failures{};
    Impl(DiscordConnection& owner,std::shared_ptr<State> state,QNetworkAccessManager* network)
        :owner(owner),state(std::move(state)),network(network?network:new QNetworkAccessManager(&owner)) {
        timer.setInterval(250);QObject::connect(&timer,&QTimer::timeout,&owner,[this] { tick(); });
    }
    void status(QString value) { if (message!=value) { message=std::move(value);if(owner.on_changed) owner.on_changed(); } }
    void publish(Clock::time_point now) {
        std::erase_if(peers,[&](const auto& member) { return member.second.second<=now; });
        Snapshot snapshot{hosting?Role::host:Role::guest,{}};
        self.topics=state->publication().topics;snapshot.peers.push_back(self);
        for (const auto& [id,entry]:peers) snapshot.peers.push_back(entry.first.peer);
        if(state->receive(std::move(snapshot),now) && owner.on_changed) owner.on_changed();
    }
    void cancel() {
        ++generation;timer.stop();enabled=false;halted=false;
        if (pending) { auto reply=pending.data();pending.clear();reply->disconnect(&owner);reply->abort();reply->deleteLater(); }
        peers.clear();latest.clear();token.clear();bot_id.clear();message_id.clear();group={};self={};encoded_invite.clear();state->disconnect();
    }
    void tick() {
        if (!enabled) return;
        const auto now=Clock::now();publish(now);
        if (pending || halted || now<blocked_until) return;
        if (bot_id.isEmpty()) { request("GET","/users/@me",{},true);return; }
        if (read_next && now>=read_at) {
            read_next=false;request("GET","/channels/"+group.channel+"/messages?limit=100");return;
        }
        if (now>=write_at) {
            try {
                const auto local=state->publication();self.topics=local.topics;
                last_sent=std::max(last_sent+1,QDateTime::currentMSecsSinceEpoch()+server_offset);
                const auto content=seal_discord_packet(group,{self,last_sent});
                QJsonObject body{{"content",content},{"allowed_mentions",QJsonObject{{"parse",QJsonArray{}}}}};
                if(message_id.isEmpty()) {
                    // Retry a timed-out create without creating duplicate messages.
                    body["nonce"]=create_nonce;body["enforce_nonce"]=true;
                }
                request(message_id.isEmpty()?"POST":"PATCH","/channels/"+group.channel+"/messages"+(message_id.isEmpty()?QString{}:"/"+message_id),body);
            } catch (const std::exception& e) { halted=true;status(QString::fromUtf8(e.what())); }
            return;
        }
        if (now>=read_at) { read_next=true;tick(); }
    }
    void request(QByteArray method,QString path,QJsonObject body={},bool authentication=false) {
        QNetworkRequest request(QUrl("https://discord.com/api/v10"+path));
        request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
        request.setRawHeader("Authorization","Bot "+token.toUtf8());
        request.setRawHeader("User-Agent","Aurex Alt Sync (https://github.com/fenixJK/Aurex)");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(10000);
        auto* reply=network->sendCustomRequest(request,method,body.isEmpty()?QByteArray{}:QJsonDocument(body).toJson(QJsonDocument::Compact));
        QTimer::singleShot(10000,reply,[reply] { if(!reply->isFinished()) reply->abort(); });
        pending=reply;const auto revision=generation;const bool reading=method=="GET",creating=method=="POST";
        QObject::connect(reply,&QIODevice::readyRead,&owner,[reply] { if(reply->bytesAvailable()>max_response) reply->abort(); });
        QObject::connect(reply,&QNetworkReply::finished,&owner,[this,reply,revision,reading,creating,authentication] {
            reply->deleteLater();if(revision!=generation || !enabled) return;
            pending.clear();const auto now=Clock::now();
            const auto code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto bytes=reply->read(max_response+1);const auto document=QJsonDocument::fromJson(bytes);
            auto server_time=QDateTime::fromString(QString::fromLatin1(reply->rawHeader("Date")),Qt::RFC2822Date);
            const auto server_now=server_time.isValid()?server_time.toMSecsSinceEpoch():QDateTime::currentMSecsSinceEpoch()+server_offset;
            if(server_time.isValid()) server_offset=server_now-QDateTime::currentMSecsSinceEpoch();
            if(code==429) {
                const auto seconds=std::max(reply->rawHeader("Retry-After").toDouble(),document.object()["retry_after"].toDouble(1));
                blocked_until=now+delay(seconds)+std::chrono::milliseconds(QRandomGenerator::global()->bounded(250,750));
                status("Discord is busy · sync resumes after its retry delay");return;
            }
            if(reply->rawHeader("X-RateLimit-Remaining")=="0") blocked_until=now+delay(reply->rawHeader("X-RateLimit-Reset-After").toDouble());
            if(code==401 || code==403) {
                halted=true;peers.clear();publish(now);
                status(code==401?"Discord rejected the bot token. Leave the group and reconnect with a valid token.":
                    "Discord permissions missing. Allow View Channel, Read Message History and Send Messages, then reconnect.");return;
            }
            if(code==404 && !reading && !creating) { message_id.clear();create_nonce=QString::number(QRandomGenerator::system()->generate64());write_at=now+std::chrono::milliseconds(1000);if(owner.on_message)owner.on_message({});return; }
            if(code>=400 && code<500) {
                halted=true;status("Discord could not use this channel or message. Check the channel and reconnect.");return;
            }
            if(reply->error()!=QNetworkReply::NoError || code<200 || code>=300 || document.isNull() || bytes.size()>max_response) {
                blocked_until=now+std::chrono::seconds(std::min(30,1<<std::min(++failures,5)));
                status("Discord unavailable · reconnecting automatically");return;
            }
            failures=0;
            if(authentication) {
                const auto user=document.object();bot_id=user["id"].toString();
                if(!user["bot"].toBool() || !discord_id(bot_id)) { halted=true;status("Use a Discord bot token for Alt Sync.");return; }
                read_at=write_at=now;
            } else if(reading) {
                if(!document.isArray()) { blocked_until=now+heartbeat;status("Discord returned an unreadable group update · retrying");return; }
                for (const auto& item:document.array()) {
                    const auto value=item.toObject();
                    if(value["author"].toObject()["id"].toString()!=bot_id) continue;
                    try {
                        auto packet=open_discord_packet(group,value["content"].toString());
                        const auto age=server_now-packet.sent_ms;
                        if(packet.peer.id==self.id || age< -10000 || age>=15000 || (hosting && packet.peer.host)) continue;
                        if(!packet.peer.host && peers.size()>=15 && !peers.contains(packet.peer.id)) continue;
                        if(const auto seen=latest.find(packet.peer.id);seen!=latest.end() && seen->second>=packet.sent_ms) continue;
                        if(packet.peer.host) std::erase_if(peers,[](const auto& entry) { return entry.second.first.peer.host; });
                        latest[packet.peer.id]=packet.sent_ms;
                        const auto id=packet.peer.id;peers[id]={std::move(packet),now+lifetime-std::chrono::milliseconds(std::max(qint64(0),age))};
                    } catch(const std::exception&) { /* Other groups and malformed messages are not sync state. */ }
                }
                // Keep replay tracking bounded even in a channel with many old participants.
                if(latest.size()>128) std::erase_if(latest,[&](const auto& entry) { return !peers.contains(entry.first); });
                read_at=now+poll_interval;
            } else {
                const auto id=document.object()["id"].toString();
                if(!discord_id(id)) { blocked_until=now+heartbeat;status("Discord did not confirm the sync message · retrying");return; }
                if(message_id!=id) { message_id=id;if(owner.on_message)owner.on_message(id); }
                write_at=now+heartbeat;read_next=true;
            }
            publish(now);
            const bool main=hosting || std::any_of(peers.begin(),peers.end(),[](const auto& p) { return p.second.first.peer.host; });
            status(main?"Connected through Discord":"Discord connected · waiting for main");
        });
    }
};
DiscordConnection::DiscordConnection(std::shared_ptr<State> state,QObject* parent,QNetworkAccessManager* network)
    :QObject(parent),impl_(std::make_unique<Impl>(*this,std::move(state),network)) {}
DiscordConnection::~DiscordConnection() { on_changed={};on_message={};impl_->cancel(); }
void DiscordConnection::connect_group(DiscordInvite invite,QString name,QString peer_id,QString token,QString message_id) {
    const auto encoded=encode_discord_invite(invite);
    token=token.trimmed();if(token.startsWith("Bot "))token.remove(0,4);
    if(token.isEmpty() || token.size()>256 || std::any_of(token.begin(),token.end(),[](QChar c) { return c.isSpace() || c.category()==QChar::Other_Control; }))
        throw std::invalid_argument("Enter the Discord bot token used by this group.");
    if(!message_id.isEmpty() && !discord_id(message_id)) throw std::invalid_argument("Invalid saved Discord message.");
    Peer peer{peer_id.toStdString(),name.trimmed().toStdString(),!invite.identity.key.isEmpty(),{}};
    const auto check=seal_discord_packet(invite,{peer,QDateTime::currentMSecsSinceEpoch()});open_discord_packet(invite,check);
    impl_->cancel();impl_->group=std::move(invite);impl_->self=std::move(peer);impl_->hosting=impl_->self.host;impl_->encoded_invite=encoded;
    impl_->token=std::move(token);impl_->message_id=std::move(message_id);impl_->enabled=true;
    impl_->create_nonce=QString::number(QRandomGenerator::system()->generate64());
    impl_->failures=0;impl_->server_offset=0;impl_->last_sent=0;impl_->blocked_until={};impl_->read_next=true;
    impl_->status("Connecting through Discord…");impl_->timer.start();
}
void DiscordConnection::leave() { impl_->cancel();impl_->status("Not connected"); }
QString DiscordConnection::status() const { return impl_->message; }
QString DiscordConnection::invite() const { return impl_->enabled && impl_->hosting?impl_->encoded_invite:QString{}; }
}
