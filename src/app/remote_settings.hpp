#pragma once
#include "hive/reporting/remote.hpp"
#include <QPointer>
#include <QWidget>

class QSettings;
class QLineEdit;
namespace hive::app {
class RemoteSettingsPage final : public QWidget {
public:
    RemoteSettingsPage(QSettings& settings,reporting::DiscordRemote& remote,QWidget* parent=nullptr,QLineEdit* user=nullptr,
        std::function<void(const reporting::RemoteSettings&)> on_changed={});
    ~RemoteSettingsPage() override;
private:
    QPointer<reporting::DiscordRemote> remote_;
};
}
