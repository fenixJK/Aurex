#pragma once
#include "coordination/connection.hpp"
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
    coordination::Connection connection_;
    bool enabled_{},joined_{};
    QString issue_,name_,server_,epoch_;
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
    std::function<QString()> suggested_server;
    void join(QString name,QString invite);
    void leave();
    void retry();
    bool enabled() const { return enabled_; }
    bool joined() const { return joined_; }
    QString name() const { return name_; }
    QString status() const;
    QString invite() const { return enabled_?connection_.invite():QString{}; }
    bool mapping_failed() const { return enabled_ && connection_.mapping_failed(); }
    const std::shared_ptr<coordination::State>& state() const { return state_; }
    std::function<void()> on_changed;
};
}
