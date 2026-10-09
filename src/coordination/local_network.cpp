#include "local_network.hpp"
#include <QNetworkInterface>
#include <algorithm>
#include <stdexcept>

namespace hive::coordination {
QList<LocalNetwork> local_networks() {
    auto interfaces=QNetworkInterface::allInterfaces();
    const auto rank=[](const QNetworkInterface& network) {
        return network.type()==QNetworkInterface::Ethernet || network.type()==QNetworkInterface::Wifi?0:1;
    };
    std::stable_sort(interfaces.begin(),interfaces.end(),[&](const auto& a,const auto& b) { return rank(a)<rank(b); });
    QList<LocalNetwork> result;
    for(const auto& network:interfaces) {
        const auto flags=network.flags();
        if(!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack) || flags.testFlag(QNetworkInterface::IsPointToPoint)
            || network.type()==QNetworkInterface::Virtual) continue;
        for(const auto& entry:network.addressEntries()) {
            const auto address=entry.ip();
            if(address.protocol()!=QAbstractSocket::IPv4Protocol || address.isLoopback()
                || address.toIPv4Address()<0x01000000 || address.toIPv4Address()>=0xe0000000) continue;
            result.append({network.name(),network.humanReadableName(),address});break;
        }
    }
    return result;
}
LocalNetwork local_network(const QString& id) {
    const auto networks=local_networks();
    for(const auto& network:networks) if(id.isEmpty() || network.id==id) return network;
    throw std::invalid_argument(id.isEmpty()?"Connect to Wi-Fi or Ethernet to host on this network.":
        "The selected network is disconnected. Reconnect it or choose another network.");
}
}
