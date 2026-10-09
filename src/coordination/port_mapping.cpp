#include "port_mapping.hpp"
#include <QFutureWatcher>
#include <QHostAddress>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QtConcurrentRun>
#include <atomic>
#include <algorithm>
#include <stdexcept>

namespace hive::coordination {
QString MappedPort::endpoint() const { QUrl url;url.setScheme("wss");url.setHost(address);url.setPort(port);return url.toString(); }
bool public_router_address(const QString& value) {
    bool valid=false;const auto ip=QHostAddress(value).toIPv4Address(&valid);
    return valid && (ip>>24)!=0 && (ip>>24)!=10 && (ip>>24)!=127 && (ip>>28)<14
        && (ip>>16)!=0xa9fe && (ip>>20)!=0xac1 && (ip>>16)!=0xc0a8
        && (ip>>22)!=0x191 && (ip>>17)!=0x6309;
}
struct PortMapper::Impl {
    struct Work { std::atomic_uint64_t generation{};std::unique_ptr<PortLease> lease; };
    PortMapper& owner;
    RouterMapping router;
    std::shared_ptr<Work> work=std::make_shared<Work>();
    QThreadPool pool;
    QTimer renewal;
    MappingStatus state;
    Impl(PortMapper& value,RouterMapping factory):owner(value),router(std::move(factory)) {
        pool.setMaxThreadCount(1);renewal.setSingleShot(true);
        QObject::connect(&renewal,&QTimer::timeout,&owner,[this] { request(0); });
    }
    void report(MappingStatus value) { state=std::move(value);if(owner.on_changed) owner.on_changed(); }
    void request(quint16 port) {
        const auto generation=work->generation.load();
        auto watcher=new QFutureWatcher<MappingStatus>(&owner);
        QObject::connect(watcher,&QFutureWatcher<MappingStatus>::finished,&owner,[this,watcher,generation] {
            const auto result=watcher->result();watcher->deleteLater();
            if(work->generation!=generation) return;
            report(result);
            if(result.phase==MappingPhase::ready && work->generation==generation)
                renewal.start(int(std::clamp(result.mapping.lifetime*500u,500u,1800000u)));
        });
        watcher->setFuture(QtConcurrent::run(&pool,[work=work,router=router,generation,port] {
            const MappingCancelled cancelled=[work,generation] { return work->generation!=generation; };
            if(cancelled()) return MappingStatus{};
            try {
                if(port) { work->lease.reset();work->lease=router(port,cancelled); }
                if(cancelled()) { work->lease.reset();return MappingStatus{}; }
                if(!work->lease) throw std::runtime_error("The router did not create a mapping.");
                const auto result=port?work->lease->current():work->lease->renew();
                if(!public_router_address(result.address) || !result.port || !result.lifetime || result.lifetime>7200)
                    throw std::runtime_error("The router did not provide a temporary public mapping.");
                if(cancelled()) { work->lease.reset();return MappingStatus{}; }
                return MappingStatus{MappingPhase::ready,result,"Hosting · router port opened"};
            } catch(const std::exception& error) {
                work->lease.reset();return MappingStatus{MappingPhase::unavailable,{},QString::fromUtf8(error.what())};
            }
        }));
    }
    void stop() {
        if(state.phase==MappingPhase::idle) return;
        ++work->generation;renewal.stop();
        pool.start([work=work] { work->lease.reset(); });report({});
    }
};
PortMapper::PortMapper(RouterMapping router,QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(*this,std::move(router))) {}
PortMapper::~PortMapper() { on_changed={};impl_->stop();impl_->pool.waitForDone(); }
void PortMapper::start(quint16 port) {
    if(!port) throw std::invalid_argument("Hosting requires a listening port.");
    impl_->stop();const auto generation=impl_->work->generation.load();
    impl_->report({MappingPhase::requesting,{},"Asking the router to open a port…"});
    if(impl_->work->generation==generation && impl_->state.phase==MappingPhase::requesting) impl_->request(port);
}
void PortMapper::stop() { impl_->stop(); }
MappingStatus PortMapper::status() const { return impl_->state; }
}
