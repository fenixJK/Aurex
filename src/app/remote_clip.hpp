#pragma once
#include "reporting/remote.hpp"
#include "core/runtime/model.hpp"
#include <QObject>
#include <QTimer>
#include <memory>

namespace hive::app {
class ClipRecorder final : public QObject {
public:
    explicit ClipRecorder(std::function<std::shared_ptr<const Frame>()> latest,QObject* parent=nullptr);
    ~ClipRecorder() override;
    reporting::RemoteReply record(Window window,int seconds,int screen=0);
    void cancel();
private:
    struct Recording;
    std::weak_ptr<Recording> active_;
    std::function<std::shared_ptr<const Frame>()> latest_;
    QTimer timer_;
};
}
