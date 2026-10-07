#pragma once
#include "report.hpp"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QSet>
#include <QTimer>
#include <QWebSocket>
#include <functional>

namespace hive::reporting {
struct RemoteSettings {
    bool enabled{};
    QString token,user,channel,device;
    bool operator==(const RemoteSettings&) const = default;
};
struct RemoteReply {
    QString title,description;
    QList<QPair<QString,QString>> fields;
    QImage image;
    bool inline_fields{};
};
enum class RemoteOptionType { text=3, integer=4, boolean=5, number=10 };
struct RemoteOption {
    QString name,description;
    RemoteOptionType type{RemoteOptionType::text};
    bool required{};
    QStringList choices;
    double minimum{},maximum{1000};
    bool autocomplete{};
};
using RemoteArguments=QJsonObject;
using RemoteChoices=QList<QPair<QString,QString>>;
struct RemoteCommand {
    QString name,description;
    bool control{};
    std::function<RemoteReply(const RemoteArguments&)> run;
    QList<RemoteOption> options;
    std::function<RemoteChoices(const QString&,const RemoteArguments&)> complete;
};
QString validate(const RemoteSettings& settings);
QJsonObject remote_command_definition(const RemoteCommand& command);
RemoteReply status_reply(const Report& report);
RemoteReply nectar_reply(const Report& report,std::optional<qint64> age_ms);
struct RemoteCard { QString label,value; std::optional<double> percent; QColor color; std::optional<double> floor; };
QImage remote_cards(const QString& title,const QList<RemoteCard>& cards);

class DiscordRemote final : public QObject {
public:
    explicit DiscordRemote(QList<RemoteCommand> commands,QObject* parent=nullptr,QNetworkAccessManager* network=nullptr);
    ~DiscordRemote() override;
    bool configure(const RemoteSettings& settings);
    void retry_connection();
    void set_commands(QList<RemoteCommand> commands);
    void retire_commands(QStringList names);
    const RemoteSettings& settings() const { return settings_; }
    const QList<RemoteCommand>& commands() const { return commands_; }
    const QString& connection_status() const { return status_; }
    const QString& application_id() const { return application_; }
    void cancel_commands() { ++command_revision_; }
    std::function<void(const QString&)> on_status;
private:
    using Response=std::function<void(int,const QJsonDocument&)>;
    RemoteSettings settings_;
    QList<RemoteCommand> commands_;
    QStringList retired_commands_;
    QNetworkAccessManager* network_;
    QWebSocket socket_;
    QTimer heartbeat_,retry_,deadline_;
    QSet<QNetworkReply*> replies_;
    QString status_{"Off"},guild_,application_,session_;
    QUrl resume_url_;
    qint64 sequence_{-1};
    unsigned revision_{},failures_{},command_revision_{},registration_revision_{};
    bool registration_active_{};
    bool blocked_{},awaiting_ack_{},live_{},registered_{},preparing_image_{};
    int heartbeat_ms_{};
    QList<QString> seen_;
    void status(QString message);
    void cancel();
    void bootstrap();
    void open_gateway();
    void receive(const QString& packet);
    void send(int opcode,const QJsonValue& data);
    void heartbeat();
    void reconnect(int delay=0);
    void fail(const QString& message);
    void request(const QByteArray& method,const QString& path,const QJsonObject& data,bool authenticated,Response response,const QByteArray& image={});
    void register_commands(int index=-1,QJsonArray existing={});
    void interaction(const QJsonObject& event);
    void respond(const QString& token,RemoteReply reply);
    friend struct DiscordRemoteTest;
};
}
