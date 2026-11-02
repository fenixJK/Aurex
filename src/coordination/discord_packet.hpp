#pragma once
#include "connection.hpp"
#include <QDateTime>

namespace hive::coordination {
struct DiscordInvite {
    QString channel;
    HostIdentity identity; // Invites contain the certificate and group secret, never the private key.
};
QString encode_discord_invite(const DiscordInvite& invite);
DiscordInvite decode_discord_invite(const QString& value);
bool discord_id(const QString& value);
struct DiscordPacket { Peer peer; qint64 sent_ms{}; };
QString seal_discord_packet(const DiscordInvite& invite,const DiscordPacket& packet);
DiscordPacket open_discord_packet(const DiscordInvite& invite,const QString& content);
}
