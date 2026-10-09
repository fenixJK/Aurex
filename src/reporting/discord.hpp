#pragma once
#include "core/runtime/runtime.hpp"
#include "core/runtime/activity_status.hpp"
#include "report.hpp"
#include <QDateTime>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <deque>
#include <functional>
#include <map>
#include <optional>

class QNetworkReply;
namespace hive::reporting {
struct DiscordSettings {
    static constexpr int default_report_minutes=60;
    bool enabled{},errors{true},screenshots{},live_honey{},error_screenshots{true};
    int report_minutes{default_report_minutes};
    QString webhook,mention,report_channel;
    QStringList buff_ids;
    bool operator==(const DiscordSettings&) const = default;
};
QString validate(const DiscordSettings& settings,bool webhook_required=true);
struct BotDestination {
    QString token,channel;
    bool operator==(const BotDestination&) const = default;
};
QString redact(QString text);
QUrl webhook_url(const QString& input);
QJsonObject report_payload(const Report& report);
QDateTime next_report_time(const QDateTime& now,int minutes);

class Discord final : public QObject {
public:
    using SnapshotSource=std::function<Snapshot()>;
    using ImageSource=std::function<QImage()>;
    Discord(SnapshotSource snapshot,ImageSource image={},QObject* parent=nullptr,QNetworkAccessManager* network=nullptr);
    ~Discord() override;
    bool configure(const DiscordSettings& settings);
    void set_bot(std::optional<BotDestination> destination);
    bool uses_bot() const { return bot_.has_value(); }
    void set_live_image_source(ImageSource source) { live_image_=std::move(source); }
    const DiscordSettings& settings() const { return settings_; }
    const QString& delivery_status() const { return delivery_status_; }
    std::function<void(const QString&)> on_delivery;
    void set_history_requested(bool requested);
    void update(RunState state,std::uint64_t session_id,const QString& reason={},const QString& activity={});
    void activity(const QString& message);
    void activity_status(const ActivityStatus& status);
    void activity_event(const ActivityStatus& status,const QImage& screenshot={});
    void notice(const QString& message);
    void error(const QString& message,std::optional<QImage> screenshot=std::nullopt);
    void send_report();
    Report preview_report();
    QList<Gauge> available_buffs() const;
private:
    struct Images { QByteArray report,game; };
    struct Message { QJsonObject body; QString key; bool screenshot{}; unsigned attempts{}; qint64 expires{}; std::optional<Report> report; QImage game; std::uint64_t live_revision{}; std::optional<Images> images; };
    DiscordSettings settings_;
    std::optional<BotDestination> bot_;
    SnapshotSource snapshot_;
    ImageSource image_;
    ImageSource live_image_;
    QNetworkAccessManager* network_;
    QPointer<QNetworkReply> reply_;
    QTimer period_,retry_,deadline_;
    QDateTime report_due_;
    QElapsedTimer clock_,session_clock_;
    qint64 elapsed_{},next_send_{};
    qint64 next_sample_{};
    History history_;
    std::uint64_t session_id_{},revision_{};
    RunState state_{RunState::idle};
    bool preparing_{},blocked_{},history_requested_{};
    QString delivery_status_{"Off"};
    ActivityStatus last_activity_;
    QString live_message_;
    std::uint64_t live_revision_{1};
    bool live_active_{},live_failed_{};
    qint64 next_live_{};
    std::deque<Message> queue_;
    std::map<QString,qint64> errors_;
    void status(QString message);
    bool record_alert(const QString& key);
    void enqueue(Message message);
    void pump();
    void post(Images images,std::uint64_t revision);
    void completed(QNetworkReply* reply,std::uint64_t revision);
    void cancel();
    void schedule_reports();
    void update_live(bool active);
    QString destination_issue() const;
};
}
