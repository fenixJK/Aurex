#pragma once
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

namespace hive::coordination {
struct MappedPort {
    QString address;
    quint16 port{};
    unsigned lifetime{};
    QString endpoint() const;
};
class PortLease {
public:
    virtual ~PortLease()=default;
    virtual MappedPort current() const=0;
    virtual MappedPort renew()=0;
};
using MappingCancelled=std::function<bool()>;
using RouterMapping=std::function<std::unique_ptr<PortLease>(quint16,const MappingCancelled&)>;
RouterMapping router_mapping();
bool public_router_address(const QString& address);
enum class MappingPhase { idle, requesting, ready, unavailable };
struct MappingStatus { MappingPhase phase{MappingPhase::idle};MappedPort mapping;QString message; };
class PortMapper final : public QObject {
public:
    explicit PortMapper(RouterMapping router=router_mapping(),QObject* parent=nullptr);
    ~PortMapper() override;
    void start(quint16 port);
    void stop();
    MappingStatus status() const;
    std::function<void()> on_changed;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
