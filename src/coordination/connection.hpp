#pragma once
#include "state.hpp"
#include "port_mapping.hpp"
#include <QObject>
#include <QHostAddress>
#include <QString>
#include <memory>
#include <functional>

namespace hive::coordination {
struct HostIdentity { QByteArray certificate,key; QString token; };
HostIdentity create_identity();
QString save_identity(const HostIdentity& identity);
HostIdentity load_identity(const QString& value);
struct Invite { QString endpoint,fingerprint,token; };
QString encode_invite(const Invite& value);
Invite decode_invite(const QString& value);
QString validate_endpoint(const QString& value);

class Connection final : public QObject {
public:
    explicit Connection(std::shared_ptr<State> state,QObject* parent=nullptr,RouterMapping router=router_mapping());
    ~Connection() override;
    QString host(const HostIdentity& identity,const QString& name,const QString& endpoint={},
        const QHostAddress& bind=QHostAddress::Any,quint16 port=45831);
    QString host_local(const HostIdentity& identity,const QString& name,const QString& network={},quint16 port=45831);
    void join(const Invite& invite,const QString& name);
    void leave();
    QString status() const;
    bool hosting() const;
    QString invite() const;
    bool mapping_failed() const;
    void retry_mapping();
    std::function<void()> on_changed;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
