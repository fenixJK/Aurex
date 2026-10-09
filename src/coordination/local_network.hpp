#pragma once
#include <QHostAddress>
#include <QList>
#include <QString>

namespace hive::coordination {
struct LocalNetwork { QString id,name;QHostAddress address; };
QList<LocalNetwork> local_networks();
LocalNetwork local_network(const QString& id={});
}
