#include "hive/coordination/port_mapping.hpp"
#include <QHostAddress>
#include <QRandomGenerator>
#include <QUuid>
#include <miniupnpc.h>
#include <upnpcommands.h>
#include <natpmp.h>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace hive::coordination {
namespace {
constexpr unsigned lease_seconds=1800;
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
void check_cancelled(const MappingCancelled& cancelled) { if(cancelled()) throw std::runtime_error("Hosting cancelled."); }
#ifdef _WIN32
struct SocketRuntime {
    SocketRuntime() { WSADATA data{};require(WSAStartup(MAKEWORD(2,2),&data)==0,"Could not start router networking."); }
    ~SocketRuntime() { WSACleanup(); }
};
#else
struct SocketRuntime {};
#endif
class UpnpLease final : public PortLease {
    SocketRuntime sockets_;
    UPNPUrls urls_{};
    IGDdatas data_{};
    QByteArray local_,internal_,external_,description_;
    MappedPort mapping_;
    bool attempted_{};
    struct Entry { char client[64]{},port[6]{},description[80]{},enabled[4]{},lifetime[16]{}; };
    int read(Entry& entry) const {
        return UPNP_GetSpecificPortMappingEntry(urls_.controlURL,data_.first.servicetype,external_.constData(),"TCP",nullptr,
            entry.client,entry.port,entry.description,entry.enabled,entry.lifetime);
    }
    bool owned(const Entry& entry) const {
        return local_==entry.client && internal_==entry.port && description_==entry.description;
    }
    void add() {
        attempted_=true;
        const auto result=UPNP_AddPortMapping(urls_.controlURL,data_.first.servicetype,external_.constData(),internal_.constData(),
            local_.constData(),description_.constData(),"TCP",nullptr,"1800");
        require(result!=725,"This router only allows permanent mappings. Automatic hosting needs temporary mappings.");
        require(result==UPNPCOMMAND_SUCCESS,"The router refused to open the hosting port.");
        Entry entry;require(read(entry)==UPNPCOMMAND_SUCCESS && owned(entry) && QByteArray(entry.enabled)=="1","The router did not confirm the requested mapping.");
        bool valid=false;const auto lifetime=QByteArray(entry.lifetime).toUInt(&valid);
        require(valid && lifetime>0 && lifetime<=7200,"The router did not grant a temporary mapping.");mapping_.lifetime=lifetime;
    }
public:
    ~UpnpLease() override {
        if(attempted_ && urls_.controlURL) {
            Entry entry;
            if(read(entry)==UPNPCOMMAND_SUCCESS && owned(entry))
                UPNP_DeletePortMapping(urls_.controlURL,data_.first.servicetype,external_.constData(),"TCP",nullptr);
        }
        FreeUPNPUrls(&urls_);
    }
    void open(quint16 port,const MappingCancelled& cancelled) {
        int error{};
        std::unique_ptr<UPNPDev,decltype(&freeUPNPDevlist)> devices(upnpDiscover(1500,nullptr,nullptr,0,0,2,&error),freeUPNPDevlist);
        check_cancelled(cancelled);require(bool(devices),"No router supporting automatic port forwarding was found.");
        auto* cursor=devices.get();for(int count=1;cursor && count<8;++count) cursor=cursor->pNext;
        if(cursor && cursor->pNext) { freeUPNPDevlist(cursor->pNext);cursor->pNext=nullptr; }
        char local[64]{},external[64]{};
        const auto result=UPNP_GetValidIGD(devices.get(),&urls_,&data_,local,sizeof(local),external,sizeof(external));
        check_cancelled(cancelled);
        require(result!=UPNP_PRIVATEIP_IGD,"The router is behind another network or ISP NAT. It did not provide a public address.");
        require(result==UPNP_CONNECTED_IGD && urls_.controlURL,"No connected router supporting automatic port forwarding was found.");
        require(public_router_address(QString::fromLatin1(external)),"The router did not provide a public address.");
        local_=local;internal_=QByteArray::number(port);description_="Aurex "+QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1();
        bool available=false;
        for(int attempt=0;attempt<4;++attempt) {
            check_cancelled(cancelled);external_=QByteArray::number(attempt?QRandomGenerator::system()->bounded(49152,65536):port);
            Entry entry;const auto code=read(entry);
            if(code==714) { available=true;break; }
            require(code==UPNPCOMMAND_SUCCESS,"The router could not check whether the hosting port is available.");
        }
        require(available,"The router's requested ports are already in use.");
        check_cancelled(cancelled);mapping_={QString::fromLatin1(external),quint16(external_.toUInt()),lease_seconds};add();
    }
    MappedPort current() const override { return mapping_; }
    MappedPort renew() override {
        Entry entry;require(read(entry)==UPNPCOMMAND_SUCCESS && owned(entry),"The router's hosting mapping was removed or changed. Try opening it again.");
        add();char address[64]{};
        require(UPNP_GetExternalIPAddress(urls_.controlURL,data_.first.servicetype,address)==UPNPCOMMAND_SUCCESS,"Could not read the router's public address.");
        mapping_.address=QString::fromLatin1(address);return mapping_;
    }
};
class PmpLease final : public PortLease {
    SocketRuntime sockets_;
    natpmp_t client_{};
    bool initialized_{},attempted_{};
    quint16 internal_{};
    MappedPort mapping_;
    natpmpresp_t response(unsigned type,const MappingCancelled& cancelled,std::chrono::milliseconds budget=std::chrono::milliseconds(1800)) {
        const auto deadline=std::chrono::steady_clock::now()+budget;
        while(std::chrono::steady_clock::now()<deadline) {
            check_cancelled(cancelled);natpmpresp_t value{};const auto result=readnatpmpresponseorretry(&client_,&value);
            if(result==0) { require(value.type==type,"Unexpected router response.");return value; }
            require(result==NATPMP_TRYAGAIN,"The router refused the automatic mapping request.");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        throw std::runtime_error("The router did not answer the mapping request.");
    }
    QString address(const MappingCancelled& cancelled) {
        require(sendpublicaddressrequest(&client_)>=0,"Could not request the router's public address.");
        const auto reply=response(NATPMP_RESPTYPE_PUBLICADDRESS,cancelled);
        return QHostAddress(ntohl(reply.pnu.publicaddress.addr.s_addr)).toString();
    }
    void add(const MappingCancelled& cancelled) {
        attempted_=true;
        require(sendnewportmappingrequest(&client_,NATPMP_PROTOCOL_TCP,internal_,mapping_.port,lease_seconds)>=0,"Could not request the hosting port.");
        const auto reply=response(NATPMP_RESPTYPE_TCPPORTMAPPING,cancelled).pnu.newportmapping;
        require(reply.privateport==internal_ && reply.mappedpublicport && reply.lifetime>0,"The router returned an invalid hosting mapping.");
        mapping_.port=reply.mappedpublicport;mapping_.lifetime=reply.lifetime;
    }
public:
    ~PmpLease() override {
        if(!initialized_) return;
        if(attempted_ && internal_) {
            if(sendnewportmappingrequest(&client_,NATPMP_PROTOCOL_TCP,internal_,mapping_.port,0)>=0) {
                try { response(NATPMP_RESPTYPE_TCPPORTMAPPING,[]{return false;},std::chrono::milliseconds(800)); }
                catch(const std::exception&) {}
            }
        }
        closenatpmp(&client_);
    }
    void open(quint16 port,const MappingCancelled& cancelled) {
        require(initnatpmp(&client_,0,0)==0,"Could not locate the router.");initialized_=true;
        mapping_.address=address(cancelled);require(public_router_address(mapping_.address),"The router is behind another network or ISP NAT. It did not provide a public address.");
        check_cancelled(cancelled);internal_=port;mapping_.port=port;add(cancelled);
    }
    MappedPort current() const override { return mapping_; }
    MappedPort renew() override { mapping_.address=address([] {return false;});add([] {return false;});return mapping_; }
};
}
RouterMapping router_mapping() {
    return [](quint16 port,const MappingCancelled& cancelled)->std::unique_ptr<PortLease> {
        QString issue;
        try { auto mapping=std::make_unique<UpnpLease>();mapping->open(port,cancelled);return mapping; }
        catch(const std::exception& error) { issue=QString::fromUtf8(error.what()); }
        check_cancelled(cancelled);
        try { auto mapping=std::make_unique<PmpLease>();mapping->open(port,cancelled);return mapping; }
        catch(const std::exception& error) {
            check_cancelled(cancelled);
            throw std::runtime_error(("Internet hosting unavailable. UPnP: "+issue+" NAT-PMP: "+QString::fromUtf8(error.what())
                +" Opening a computer port alone cannot bypass the router. Enable router forwarding or join a reachable host.").toStdString());
        }
    };
}
}
