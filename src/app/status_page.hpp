#pragma once
#include "reporting/discord.hpp"
#include "core/module_host/work_schedule.hpp"
#include <QWidget>

class QSettings;
class QPlainTextEdit;
class QLabel;
class QLineEdit;
class QVBoxLayout;
namespace hive::app {
class StatusPage final : public QWidget {
public:
    StatusPage(QSettings& settings,reporting::Discord& discord,QWidget* parent=nullptr,QString log_path={});
    ~StatusPage() override;
    void log(const QString& message);
    void activity(const ActivityStatus& status);
    void event(const ActivityStatus& status,const QImage& screenshot={});
    void clear_activity();
    void upcoming(const extensions::WorkSnapshot&,Time now,const std::vector<std::string>& order,const std::string& active={});
    QString recent_log(int lines=20) const;
    void add_remote(QWidget* page);
    void use_bot(std::optional<reporting::BotDestination> destination);
    QLineEdit* discord_user() const { return discord_user_; }
private:
    QPointer<reporting::Discord> discord_;
    QPlainTextEdit* log_;
    QLabel* upcoming_{};
    QStringList buff_ids_;
    reporting::ReportAppearance appearance_;
    ActivityStatus activity_;
    QString log_path_;
    QLineEdit* discord_user_{};
    QLineEdit* report_channel_{};
    QVBoxLayout* discord_layout_{};
    QWidget* webhook_row_{};
    QWidget* report_channel_row_{};
    QLabel* destination_{};
};
}
