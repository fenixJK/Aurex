#include "remote_settings.hpp"
#include <QScrollArea>
#include "controls.hpp"
#include <QDesktopServices>
#include <QDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSysInfo>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace hive::app {
RemoteSettingsPage::RemoteSettingsPage(QSettings& settings,reporting::DiscordRemote& remote,QWidget* parent,QLineEdit* shared_user,
    std::function<void(const reporting::RemoteSettings&)> on_changed)
    :QWidget(parent),remote_(&remote) {
    auto layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(8);
    auto enabled=new Toggle("Remote commands"); enabled->setObjectName("remote-enabled");
    layout->addWidget(enabled);
    auto fields=new QFormLayout; fields->setSpacing(8);layout->addLayout(fields);
    const auto entry=[&](const QString& title,const char* name,const QString& placeholder,int length) {
        auto edit=new QLineEdit; edit->setObjectName(name); edit->setPlaceholderText(placeholder); edit->setMaxLength(length); fields->addRow(title,edit); return edit;
    };
    auto token=entry("Bot token","remote-token","Paste bot token",256); token->setEchoMode(QLineEdit::Password);
    token->setToolTip("Saved locally with your other settings.");
    auto user=shared_user?shared_user:entry("Your user ID","remote-user","Only this Discord user can send commands",22);
    auto channel=entry("Command channel","remote-channel","Channel ID",22);
    channel->setToolTip("Commands, status messages and alerts use this channel. Timed reports can use a separate Report channel.");
    auto device=entry("Device","remote-device","For example: mac",32);
    auto commands=new QPushButton("Commands…");commands->setObjectName("remote-commands");
    connect(commands,&QPushButton::clicked,this,[this] {
        auto dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("Discord commands");dialog->setWindowModality(Qt::WindowModal);
        auto layout=new QVBoxLayout(dialog);auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
        auto contents=new QWidget;auto list=new QFormLayout(contents);list->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (const auto& command:remote_->commands()) {
            auto description=new QLabel(command.description);description->setWordWrap(true);description->setTextFormat(Qt::PlainText);description->setTextInteractionFlags(Qt::TextSelectableByMouse);
            list->addRow("/"+command.name,description);
        }
        scroll->setWidget(contents);layout->addWidget(scroll);auto close=new QPushButton("Close");layout->addWidget(close);
        connect(close,&QPushButton::clicked,dialog,&QDialog::accept);dialog->setFixedSize(540,450);dialog->show();
    });
    device->setToolTip("A nickname for this copy of Aurex. Use it with Discord commands, such as /status device:mac.");
    auto actions=new QHBoxLayout;auto help=new QPushButton("Help…");auto retry=new QPushButton("Reconnect");
    retry->setObjectName("remote-reconnect");help->setObjectName("remote-help");
    actions->addWidget(help);actions->addWidget(commands);actions->addWidget(retry);layout->addLayout(actions);
    auto connection=new QLabel;connection->setObjectName("remote-status");connection->setWordWrap(true);connection->setTextFormat(Qt::PlainText);layout->addWidget(connection);
    auto hostname=QSysInfo::machineHostName().toLower().replace(QRegularExpression("[^a-z0-9-]"),"-").left(32);
    if (hostname.isEmpty() || !hostname.front().isLetterOrNumber()) hostname="desktop";
    token->setText(settings.value("remote/token").toString()); if(!shared_user)user->setText(settings.value("discord/user").toString()); channel->setText(settings.value("remote/channel").toString());
    device->setText(settings.value("remote/device",hostname).toString()); enabled->setChecked(settings.value("remote/enabled",false).toBool());
    const auto show_status=[connection](const QString& text) {
        connection->setText(text);
    };
    remote_->on_status=show_status;
    connect(token,&QLineEdit::textChanged,this,[&settings,show_status](const QString& value) {
        if(value.trimmed().isEmpty()) settings.remove("remote/token");
        else settings.setValue("remote/token",value.trimmed());
        settings.sync();
        if(settings.status()!=QSettings::NoError) show_status("Could not save settings. Check that your settings location is writable.");
    });
    const auto save=[this,&settings,enabled,token,user,channel,device,show_status,on_changed] {
        if(!remote_)return false;
        reporting::RemoteSettings value{enabled->isChecked(),token->text().trimmed(),user->text().trimmed(),channel->text().trimmed(),device->text().trimmed()};
        settings.setValue("remote/enabled",value.enabled);
        settings.setValue("discord/user",value.user);
        settings.setValue("remote/channel",value.channel);settings.setValue("remote/device",value.device);
        settings.sync();
        if(on_changed)on_changed(value);
        auto issue=settings.status()!=QSettings::NoError?QString("Could not save settings. Check that your settings location is writable."):reporting::validate(value);
        if(!issue.isEmpty()) {
            value.enabled=false;remote_->configure(value);show_status(issue);return false;
        }
        remote_->configure(value);return true;
    };
    show_status(remote_->connection_status());
    save();
    connect(retry,&QPushButton::clicked,this,[this,save,enabled] {
        if(!enabled->isChecked())enabled->setChecked(true);
        else if(save())remote_->retry_connection();
    });
    for (auto* edit:{token,user,channel,device}) connect(edit,&QLineEdit::editingFinished,this,save);
    connect(enabled,&QCheckBox::toggled,this,save);
    connect(help,&QPushButton::clicked,this,[this] {
        auto dialog=new QDialog(this); dialog->setObjectName("discord-help");dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle("Discord help"); dialog->setWindowModality(Qt::WindowModal);
        auto layout=new QVBoxLayout(dialog); layout->setContentsMargins(16,16,16,16); layout->setSpacing(12);
        auto guide=new QTextBrowser;guide->setObjectName("discord-help-text");guide->setFrameShape(QFrame::NoFrame);
        guide->setHtml("<h3>Connect your bot</h3>"
            "<ol><li>Open the <b>Discord Developer Portal</b> and create an application. Under <b>Bot</b>, copy its token into Aurex’s Bot token field.</li>"
            "<li>Under <b>OAuth2 → URL Generator</b>, select <b>bot</b> and <b>applications.commands</b>. Select <b>View Channels, Send Messages, Embed Links</b> and <b>Attach Files</b>. Open the generated link to add the bot to your server.</li>"
            "<li>In Discord, enable <b>Settings → Advanced → Developer Mode</b>. Right-click your profile to copy your <b>User ID</b>, and the channel to copy its <b>Channel ID</b>.</li>"
            "<li>Enter those IDs in Aurex, choose a short <b>Device</b> name, and enable <b>Remote commands</b>.</li></ol>"
            "<h3>Choose where messages go</h3>"
            "<p><b>Command channel:</b> commands, status messages and alerts.<br>"
            "<b>Report channel:</b> timed reports only. Leave blank to use the command channel.<br>"
            "<b>Webhook:</b> used when remote commands are off.</p>"
            "<h3>Use commands</h3><p>Type <b>/status</b> in your command channel and select this device. "
            "Use <b>/help</b> for the command list, or the Commands button in Aurex. Replies are visible to the channel.</p>"
            "<p>Keep Aurex open. Only the configured User ID can control it. No privileged intents or Interactions Endpoint URL are needed.</p>");
        layout->addWidget(guide,1);
        auto buttons=new QHBoxLayout; auto portal=new QPushButton("Developer Portal"); buttons->addWidget(portal); buttons->addStretch(); auto done=new QPushButton("Done"); buttons->addWidget(done); layout->addLayout(buttons);
        connect(portal,&QPushButton::clicked,dialog,[] { QDesktopServices::openUrl(QUrl("https://discord.com/developers/applications")); });
        connect(done,&QPushButton::clicked,dialog,&QDialog::accept); dialog->setFixedSize(520,440); dialog->show();
    });
}
RemoteSettingsPage::~RemoteSettingsPage() {
    if(remote_)remote_->on_status={};
    for(auto* edit:findChildren<QLineEdit*>())edit->disconnect(this);
}
}
