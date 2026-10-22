#pragma once
#include "update_release.hpp"
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QFile>
#include <functional>
#include <memory>
class QNetworkReply;
class QProgressDialog;
class QTemporaryDir;
class QPushButton;
class QToolButton;
class QWidget;

namespace hive::app {
class Updates final : public QObject {
    QWidget* window_;
    std::function<bool()> busy_;
    QNetworkAccessManager* network_;
    QPointer<QNetworkReply> reply_;
    QPointer<QProgressDialog> progress_;
    QToolButton* version_;
    QPushButton* update_;
    std::optional<updates::Release> release_;
    std::shared_ptr<QTemporaryDir> stage_;
    QFile download_;
    QString error_;
    bool installing_{};
    bool canceled_{};
    void check(bool manual);
    void offer();
    void install();
    void fail(const QString& error);
    void prepare();
    void handoff();
public:
    Updates(QWidget* window,std::function<bool()> busy,QNetworkAccessManager* network=nullptr);
    QWidget* version_row();
    bool installing() const { return installing_; }
};
}
