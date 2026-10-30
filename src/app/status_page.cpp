#include "status_page.hpp"
#include "controls.hpp"
#include "report_appearance.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QDialog>
#include <QGridLayout>
#include <QIcon>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <algorithm>
#include <tuple>

namespace hive::app {
StatusPage::StatusPage(QSettings& settings,reporting::Discord& discord,QWidget* parent,QString log_path):QWidget(parent),discord_(&discord),log_path_(std::move(log_path)) {
    auto layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
    auto tabs=new QTabWidget; tabs->setObjectName("status-tabs"); layout->addWidget(tabs);
    auto activity=new QWidget; auto activity_layout=new QVBoxLayout(activity); activity_layout->setContentsMargins(0,4,0,0);
    upcoming_=new QLabel; upcoming_->setObjectName("upcoming-work"); upcoming_->setTextFormat(Qt::PlainText);
    upcoming_->setWordWrap(true); upcoming_->hide(); activity_layout->addWidget(upcoming_);
    log_=new QPlainTextEdit; log_->setObjectName("activity-log"); log_->setReadOnly(true); log_->setMaximumBlockCount(100);
    if (!log_path_.isEmpty()) {
        QFile file(log_path_);
        if (file.open(QIODevice::ReadOnly)) {
            if (file.size()>65536) { file.seek(file.size()-65536); file.readLine(); }
            log_->setPlainText(QString::fromUtf8(file.readAll()));
        }
    }
    activity_layout->addWidget(log_,1);
    auto log_buttons=new QHBoxLayout; log_buttons->addStretch(); auto clear=new QPushButton("Clear log"); log_buttons->addWidget(clear); activity_layout->addLayout(log_buttons);
    connect(clear,&QPushButton::clicked,this,[this] {
        log_->clear();
        if (!log_path_.isEmpty()) { QFile file(log_path_); file.open(QIODevice::WriteOnly|QIODevice::Truncate); }
    });
    tabs->addTab(activity,"Activity");
    auto panel=new QWidget; auto columns=new QHBoxLayout(panel);columns->setContentsMargins(0,4,0,0);columns->setSpacing(18);
    auto connection=new QWidget;discord_layout_=new QVBoxLayout(connection);discord_layout_->setContentsMargins(0,0,0,0);discord_layout_->setSpacing(8);
    columns->addWidget(connection,1);
    auto separator=new QFrame;separator->setFrameShape(QFrame::VLine);columns->addWidget(separator);
    auto reporting=new QWidget;auto form=new QVBoxLayout(reporting);form->setContentsMargins(0,0,0,0);form->setSpacing(8);columns->addWidget(reporting,1);
    panel->setStyleSheet("QLineEdit { background:#121c20; border:1px solid #34444d; border-radius:5px; padding:5px; } QLineEdit:focus { border-color:#89b99a; } QLineEdit:disabled { background:#192226; color:#617277; border-color:#29363b; } QLabel:disabled { color:#617277; }");
    auto account=new QFormLayout;
    discord_user_=new QLineEdit;discord_user_->setObjectName("discord-user");discord_user_->setMaxLength(22);
    discord_user_->setPlaceholderText("Your Discord user ID");
    discord_user_->setToolTip("Only this user can send commands. Error pings use the same ID.");
    auto user=settings.value("discord/user").toString();
    discord_user_->setText(user);
    account->addRow("User ID",discord_user_);discord_layout_->addLayout(account);
    auto enabled=new Toggle("Reports & alerts"); enabled->setObjectName("discord-enabled"); form->addWidget(enabled);
    auto fields=new QFormLayout; fields->setSpacing(8);
    auto url=new QLineEdit; url->setObjectName("discord-webhook"); url->setEchoMode(QLineEdit::Password); url->setMaxLength(512);
    url->setPlaceholderText("Paste webhook URL");
    url->setToolTip("In your Discord server: Server Settings → Integrations → Webhooks. Copy the webhook URL here.");
    webhook_row_=new QWidget;auto webhook_form=new QFormLayout(webhook_row_);webhook_form->setContentsMargins(0,0,0,0);webhook_form->addRow("Webhook",url);
    discord_layout_->addWidget(webhook_row_);
    destination_=new QLabel;destination_->setWordWrap(true);destination_->setObjectName("discord-destination");discord_layout_->addWidget(destination_);discord_layout_->addStretch();
    report_channel_=new QLineEdit;report_channel_->setObjectName("discord-report-channel");report_channel_->setMaxLength(22);
    report_channel_->setPlaceholderText("Same as command channel");
    report_channel_->setToolTip("Timed reports only. Leave blank to use the command channel. Status messages and alerts stay in the command channel.");
    report_channel_row_=new QWidget; auto channel_form=new QFormLayout(report_channel_row_); channel_form->setContentsMargins(0,0,0,0); channel_form->addRow("Report channel",report_channel_); fields->addRow(report_channel_row_);
    auto interval=new QComboBox; interval->setObjectName("discord-interval");
    for (int minutes:{15,30,60,120,240,360,720})
        interval->addItem(minutes<60?QString("Every %1 min").arg(minutes):minutes==60?"Every hour":QString("Every %1 hours").arg(minutes/60),minutes);
    fields->addRow("Reports",interval);
    auto buffs=new QPushButton("Choose buffs…"); buffs->setObjectName("discord-buffs");
    auto buff_count=new QLabel; buff_count->setObjectName("muted");
    auto buff_row=new QHBoxLayout; buff_row->addWidget(buffs); buff_row->addWidget(buff_count); buff_row->addStretch();
    fields->addRow(buff_row);
    auto appearance=new QPushButton("Report layout…"); appearance->setObjectName("report-appearance"); fields->addRow(appearance);
    form->addLayout(fields);
    auto mention=new QCheckBox("Ping me on error");mention->setObjectName("discord-ping");
    mention->setToolTip("Recoverable interruptions are reported without a ping. Failures that need attention can still ping you.");
    auto notifications=new QGridLayout;
    auto errors=new QCheckBox("Error alerts"); errors->setObjectName("discord-errors");
    notifications->addWidget(errors,0,0);notifications->addWidget(mention,1,0);
    auto error_screenshots=new QCheckBox("Screenshot on error"); error_screenshots->setObjectName("discord-error-screenshots");
    error_screenshots->setToolTip("Attach the Roblox window when an action fails, including failures the macro recovers from.");
    notifications->addWidget(error_screenshots,2,0);
    auto screenshots=new QCheckBox("Screenshot with reports"); screenshots->setObjectName("discord-screenshots");
    screenshots->setToolTip("Adds the Roblox game frame to timed reports.");
    notifications->addWidget(screenshots,3,0);form->addLayout(notifications);
    auto live_honey=new QCheckBox("Live honey && backpack"); live_honey->setObjectName("discord-live-honey");
    live_honey->setToolTip("Updates one Discord message every 5 seconds while gathering or converting, using the in-game honey and backpack bars.");
    form->addWidget(live_honey);
    auto delivery=new QLabel; delivery->setObjectName("discord-delivery"); delivery->setWordWrap(true); delivery->setTextFormat(Qt::PlainText); form->addWidget(delivery);
    form->addStretch(); tabs->addTab(panel,"Discord");
    reporting::DiscordSettings initial;
    initial.enabled=settings.value("discord/enabled",false).toBool(); initial.webhook=settings.value("discord/webhook").toString();
    initial.report_channel=settings.value("discord/reportChannel").toString();report_channel_->setText(initial.report_channel);
    initial.errors=settings.value("discord/errors",true).toBool();
    initial.screenshots=settings.value("discord/screenshots",false).toBool();
    mention->setChecked(settings.value("discord/ping",false).toBool());
    initial.mention=mention->isChecked()?user:QString{};
    initial.live_honey=settings.value("discord/liveHoney",false).toBool();
    initial.error_screenshots=settings.value("discord/errorScreenshots",true).toBool();
    initial.report_minutes=settings.value("discord/reportMinutes",reporting::DiscordSettings::default_report_minutes).toInt();
    if (settings.contains("discord/buffs")) buff_ids_=settings.value("discord/buffs").toStringList();
    else for (const auto& buff:discord_->available_buffs()) buff_ids_.append(buff.id.isEmpty()?buff.name:buff.id);
    const auto theme=reporting::ReportTheme(settings.value("discord/theme",0).toInt());
    appearance_.theme=reporting::supported_report_theme(theme)?theme:reporting::ReportTheme::forest;
    appearance_.chart_height=std::clamp(settings.value("discord/chartHeight",100).toInt(),75,150);
    appearance_.activity=settings.value("discord/showActivity",true).toBool(); appearance_.nectar=settings.value("discord/showNectar",true).toBool();
    appearance_.modules=settings.value("discord/showModules",true).toBool(); appearance_.totals=settings.value("discord/showTotals",true).toBool();
    appearance_.background_image=settings.value("discord/backgroundImage").toString();
    appearance_.background_fade=std::clamp(settings.value("discord/backgroundFade",75).toInt(),0,100);
    appearance_.background_zoom=std::clamp(settings.value("discord/backgroundZoom",100).toInt(),reporting::ReportAppearance::min_background_zoom,reporting::ReportAppearance::max_background_zoom);
    appearance_.background_x=std::clamp(settings.value("discord/backgroundX",.5).toDouble(),0.,1.);
    appearance_.background_y=std::clamp(settings.value("discord/backgroundY",.5).toDouble(),0.,1.);
    initial.appearance=appearance_;
    initial.buff_ids=buff_ids_; settings.setValue("discord/buffs",buff_ids_);
    buff_count->setText(QString("%1 selected").arg(buff_ids_.size()));
    if (interval->findData(initial.report_minutes)<0) initial.report_minutes=reporting::DiscordSettings::default_report_minutes;
    settings.setValue("discord/reportMinutes",initial.report_minutes);
    url->setText(initial.webhook); interval->setCurrentIndex(interval->findData(initial.report_minutes));
    errors->setChecked(initial.errors); screenshots->setChecked(initial.screenshots); error_screenshots->setChecked(initial.error_screenshots);
    live_honey->setChecked(initial.live_honey);
    discord_->on_delivery=[delivery](const QString& message) { delivery->setText(message); };
    use_bot(settings.value("remote/enabled",false).toBool()?std::optional(reporting::BotDestination{}):std::nullopt);
    const bool loaded=discord_->configure(initial); enabled->setChecked(loaded && initial.enabled);
    delivery->setText(discord_->delivery_status());
    const auto save=[this,&settings,enabled,url,interval,mention,errors,screenshots,live_honey,error_screenshots] {
        reporting::DiscordSettings value;
        const auto user=discord_user_->text().trimmed();settings.setValue("discord/user",user);
        value.enabled=enabled->isChecked(); value.webhook=url->text().trimmed(); value.mention=mention->isChecked()?user:QString{};
        value.report_channel=report_channel_->text().trimmed();
        value.errors=errors->isChecked(); value.error_screenshots=error_screenshots->isChecked(); value.screenshots=screenshots->isChecked(); value.report_minutes=interval->currentData().toInt();
        value.live_honey=live_honey->isChecked();
        value.buff_ids=buff_ids_; value.appearance=appearance_;
        if (!value.enabled && !reporting::validate(value,!discord_->uses_bot()).isEmpty()) {
            const auto previous=discord_->settings(); value.webhook=previous.webhook; value.mention=previous.mention;value.report_channel=previous.report_channel;
        }
        if (!discord_->configure(value)) {
            const QSignalBlocker block(enabled); enabled->setChecked(discord_->settings().enabled); return false;
        }
        settings.setValue("discord/enabled",value.enabled); settings.setValue("discord/webhook",value.webhook);
        settings.setValue("discord/reportChannel",value.report_channel);
        settings.setValue("discord/errors",value.errors); settings.setValue("discord/errorScreenshots",value.error_screenshots);
        settings.setValue("discord/screenshots",value.screenshots); settings.setValue("discord/ping",mention->isChecked());
        settings.setValue("discord/liveHoney",value.live_honey);
        settings.setValue("discord/reportMinutes",value.report_minutes);
        settings.setValue("discord/buffs",value.buff_ids);
        settings.setValue("discord/theme",int(appearance_.theme)); settings.setValue("discord/chartHeight",appearance_.chart_height);
        settings.setValue("discord/showActivity",appearance_.activity); settings.setValue("discord/showNectar",appearance_.nectar);
        settings.setValue("discord/showModules",appearance_.modules); settings.setValue("discord/showTotals",appearance_.totals);
        settings.setValue("discord/backgroundImage",appearance_.background_image); settings.setValue("discord/backgroundFade",appearance_.background_fade);
        settings.setValue("discord/backgroundZoom",appearance_.background_zoom);
        settings.setValue("discord/backgroundX",appearance_.background_x); settings.setValue("discord/backgroundY",appearance_.background_y);
        return true;
    };
    connect(enabled,&QCheckBox::toggled,this,[save] { save(); });
    connect(url,&QLineEdit::editingFinished,this,[save] { save(); });
    connect(report_channel_,&QLineEdit::editingFinished,this,[this,save] {
        if(!save())report_channel_->setText(discord_->settings().report_channel);
    });
    connect(discord_user_,&QLineEdit::editingFinished,this,[save] { save(); });
    connect(mention,&QCheckBox::toggled,this,[save] { save(); });
    connect(interval,&QComboBox::currentIndexChanged,this,[save] { save(); });
    connect(appearance,&QPushButton::clicked,this,[this,save,&settings] {
        auto selected=discord_->available_buffs();
        selected.removeIf([this](const auto& buff) { return !buff_ids_.contains(buff.id.isEmpty()?buff.name:buff.id); });
        show_report_appearance(this,appearance_,QFileInfo(settings.fileName()).absolutePath(),save,selected);
    });
    connect(buffs,&QPushButton::clicked,this,[this,save,buff_count] {
        auto dialog=new QDialog(this); dialog->setObjectName("report-buffs"); dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle("Report buffs"); dialog->setWindowModality(Qt::WindowModal);
        auto layout=new QVBoxLayout(dialog); layout->setContentsMargins(16,16,16,16); layout->setSpacing(12);
        const auto buffs=discord_->available_buffs();
        const int columns=buffs.size()>18?3:2;
        auto grid=new QGridLayout; grid->setSpacing(8); layout->addLayout(grid);
        QList<QCheckBox*> choices;
        for (const auto& buff:buffs) {
            auto choice=new QCheckBox(buff.name); choice->setObjectName("choice-tile");
            choice->setProperty("buff-id",buff.id.isEmpty()?buff.name:buff.id);
            choice->setIcon(QIcon(QPixmap::fromImage(buff.icon))); choice->setIconSize({26,26});
            choice->setChecked(buff_ids_.contains(choice->property("buff-id").toString()));
            choice->setMinimumHeight(42); choice->setCursor(Qt::PointingHandCursor);
            grid->addWidget(choice,choices.size()/columns,choices.size()%columns); choices.append(choice);
        }
        if (choices.empty()) layout->addWidget(new QLabel("No buff graphs available."));
        auto buttons=new QHBoxLayout;
        auto all=new QPushButton("All"); all->setObjectName("buffs-all"); buttons->addWidget(all);
        auto none=new QPushButton("None"); none->setObjectName("buffs-none"); buttons->addWidget(none);
        buttons->addStretch(); auto done=new QPushButton("Done"); done->setObjectName("primary"); buttons->addWidget(done); layout->addLayout(buttons);
        const auto update=[this,save,buff_count,choices] {
            for (const auto* choice:choices) {
                const auto id=choice->property("buff-id").toString(); buff_ids_.removeAll(id);
                if (choice->isChecked()) buff_ids_.append(id);
            }
            if (!save()) buff_ids_=discord_->settings().buff_ids;
            for (auto* choice:choices) { const QSignalBlocker block(choice); choice->setChecked(buff_ids_.contains(choice->property("buff-id").toString())); }
            buff_count->setText(QString("%1 selected").arg(buff_ids_.size()));
        };
        for (auto* choice:choices) connect(choice,&QCheckBox::toggled,dialog,update);
        const auto select=[this,choices,update](bool checked) {
            buff_ids_.clear();
            for (auto* choice:choices) { const QSignalBlocker block(choice); choice->setChecked(checked); }
            update();
        };
        connect(all,&QPushButton::clicked,dialog,[select] { select(true); });
        connect(none,&QPushButton::clicked,dialog,[select] { select(false); });
        connect(done,&QPushButton::clicked,dialog,&QDialog::accept);
        dialog->setFixedSize(columns*220,layout->sizeHint().height()); dialog->show();
    });
    for (auto checkbox:{errors,screenshots,live_honey,error_screenshots}) connect(checkbox,&QCheckBox::toggled,this,[save] { save(); });
}
StatusPage::~StatusPage() {
    if (discord_) discord_->on_delivery={};
    for(auto* edit:findChildren<QLineEdit*>())edit->disconnect(this);
}
void StatusPage::add_remote(QWidget* page) { discord_layout_->insertWidget(0,page); }
void StatusPage::use_bot(std::optional<reporting::BotDestination> destination) {
    webhook_row_->setVisible(!destination);
    report_channel_row_->setVisible(destination.has_value());
    destination_->setText(destination?"Reports sent through your bot.":"Reports use this webhook.");
    discord_->set_bot(std::move(destination));
}
void StatusPage::activity(const ActivityStatus& status) {
    if (status.empty() || status.kind==ActivityKind::diagnostic || status==activity_) return;
    activity_=status;
    log(QString::fromStdString(status.text()));
    if (discord_) discord_->activity_status(status);
}
void StatusPage::clear_activity() {
    activity_={};
}
void StatusPage::event(const ActivityStatus& status,const QImage& screenshot) {
    if (status.empty()) return;
    log(QString::fromStdString(status.text()));
    if (discord_) discord_->activity_event(status,screenshot);
}
void StatusPage::upcoming(const extensions::WorkSnapshot& snapshot,Time now,const std::vector<std::string>& order,const std::string& active) {
    auto offers=snapshot.offers;
    std::erase_if(offers,[&](const auto& offer) { return offer.owner==active; });
    const auto rank=[&](const auto& offer) {
        const auto ready=snapshot.wait_until(offer.owner,now);
        const auto latest=offer.work.finish_by.value_or(Time::max())-offer.work.duration;
        return std::tuple(ready>now,ready,latest<=now?latest:Time::max(),
            std::find(order.begin(),order.end(),offer.owner)-order.begin(),offer.work.duration);
    };
    std::stable_sort(offers.begin(),offers.end(),[&](const auto& a,const auto& b) { return rank(a)<rank(b); });
    QStringList lines;
    for (const auto& offer:offers) {
        if (lines.size()==3) break;
        const auto ready=snapshot.wait_until(offer.owner,now);
        QString reason;
        if (ready>now) {
            const auto minutes=std::max<std::int64_t>(1,(ready-now).count()/60000);
            reason=QString("in %1 min").arg(minutes);
            if (ready>offer.work.ready) {
                const auto window=std::max_element(snapshot.windows.begin(),snapshot.windows.end(),[](const auto& a,const auto& b) { return a.window.until<b.window.until; });
                if (window!=snapshot.windows.end()) reason+=" · preserving "+QString::fromStdString(window->window.label);
            }
        } else reason="ready";
        lines.append(QString::fromStdString(offer.work.label)+" — "+reason);
    }
    const auto text=lines.isEmpty()?QString{}:"Upcoming work\n"+lines.join('\n');
    if (upcoming_->text()!=text) upcoming_->setText(text);
    upcoming_->setVisible(!text.isEmpty());
}
QString StatusPage::recent_log(int count) const {
    const auto lines=log_->toPlainText().split('\n');
    return reporting::redact(lines.mid(std::max(qsizetype(0),lines.size()-std::clamp(count,1,100))).join('\n')).right(3400);
}
void StatusPage::log(const QString& message) {
    const auto now=QDateTime::currentDateTime(); const auto clean=reporting::redact(message);
    log_->appendPlainText(now.toString("HH:mm:ss")+"  "+clean);
    if (log_path_.isEmpty()) return;
    QDir().mkpath(QFileInfo(log_path_).absolutePath());
    QFile file(log_path_);
    if (file.size()>512*1024) {
        const auto previous=log_path_+".previous";
        QFile::remove(previous); file.rename(previous); file.setFileName(log_path_);
    }
    if (file.open(QIODevice::WriteOnly|QIODevice::Append))
        file.write((now.toString(Qt::ISODate)+"  "+clean+"\n").toUtf8());
}
}
