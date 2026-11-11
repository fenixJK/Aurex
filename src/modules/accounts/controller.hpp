#pragma once
#include "coordination/connection.hpp"
#include "coordination/discord_connection.hpp"
#include "coordination/main_boost.hpp"
#include <QJsonObject>

namespace hive::features::accounts {
enum class HostNetwork { internet,local };
struct Credentials {
    std::function<QString(const std::string&)> read;
    std::function<void(const std::string&,const QString&)> write;
};
class Controller {
    Credentials credentials_;
    std::shared_ptr<coordination::State> state_;
    std::shared_ptr<coordination::MainBoostState> main_boost_=std::make_shared<coordination::MainBoostState>();
    coordination::Connection connection_;
    coordination::DiscordConnection discord_;
    bool discord_mode_{};
    bool enabled_{},joined_{};
    coordination::Role role_{coordination::Role::offline};
    QString issue_,name_,server_,epoch_,account_id_;
    void activate(QJsonObject config,bool save);
    void changed();
public:
    explicit Controller(Credentials credentials,coordination::RouterMapping router=coordination::router_mapping(),
        std::shared_ptr<coordination::State> state=std::make_shared<coordination::State>());
    ~Controller();
    void enable();
    void disable() noexcept;
    void host(QString name,QString address,quint16 port,QString server={},HostNetwork network=HostNetwork::internet,QString network_id={});
    const QString& server() const { return server_; }
    const QString& epoch() const { return epoch_; }
    const QString& account_id() const { return account_id_; }
    std::function<QString()> suggested_server;
    void join(QString name,QString invite,QString discord_token={});
    void host_discord(QString name,QString channel,QString bot_token,QString server={});
    void leave();
    void retry();
    bool enabled() const { return enabled_; }
    bool joined() const { return joined_; }
    coordination::Role role() const { return role_; }
    QString name() const { return name_; }
    QString status() const;
    QString invite() const { return enabled_?(discord_mode_?discord_.invite():connection_.invite()):QString{}; }
    bool mapping_failed() const { return enabled_ && !discord_mode_ && connection_.mapping_failed(); }
    const std::shared_ptr<coordination::State>& state() const { return state_; }
    const std::shared_ptr<coordination::MainBoostState>& main_boost() const { return main_boost_; }
    std::function<void()> on_changed;
};
}
