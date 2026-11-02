#pragma once
#include "discord_packet.hpp"
class QNetworkAccessManager;

namespace hive::coordination {
// Discord is a transport only; field, server and fight policy remains in Alt Sync.
class DiscordConnection final : public QObject {
public:
    explicit DiscordConnection(std::shared_ptr<State> state,QObject* parent=nullptr,QNetworkAccessManager* network=nullptr);
    ~DiscordConnection() override;
    void connect_group(DiscordInvite invite,QString name,QString peer_id,QString bot_token,QString message_id={});
    void leave();
    QString status() const;
    QString invite() const;
    std::function<void()> on_changed;
    std::function<void(const QString&)> on_message;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
