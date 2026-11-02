#include "page.hpp"
#include "coordination/local_network.hpp"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>
#include <QRadioButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QTimer>
#include <algorithm>

namespace hive::features::accounts {
Page::Page(std::shared_ptr<Controller> controller,QWidget* parent)
    :QWidget(parent),controller_(std::move(controller)) {
    auto layout=new QVBoxLayout(this);layout->setContentsMargins(0,8,0,0);layout->setSpacing(12);
    auto actions=new QHBoxLayout;host_=new QPushButton("Host group…");join_=new QPushButton("Join group…");
    copy_=new QPushButton("Copy invite");leave_=new QPushButton("Leave group");retry_=new QPushButton("Try again");retry_->setObjectName("accounts-retry");
    host_->setObjectName("accounts-host");join_->setObjectName("accounts-join");copy_->setObjectName("accounts-copy");leave_->setObjectName("accounts-leave");
    actions->addWidget(host_);actions->addWidget(join_);actions->addStretch();actions->addWidget(retry_);actions->addWidget(copy_);actions->addWidget(leave_);layout->addLayout(actions);
    status_=new QLabel;status_->setObjectName("accounts-status");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);layout->addWidget(status_);
    members_=new QTableWidget(0,4);members_->setObjectName("accounts-members");
    members_->setHorizontalHeaderLabels({"Account","Role","Field","Activity"});
    members_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);members_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    members_->verticalHeader()->hide();members_->verticalHeader()->setDefaultSectionSize(30);
    members_->setEditTriggers(QAbstractItemView::NoEditTriggers);members_->setSelectionMode(QAbstractItemView::NoSelection);
    members_->setShowGrid(false);members_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);layout->addWidget(members_);
    auto refresh_timer=new QTimer(this);refresh_timer->setInterval(1000);connect(refresh_timer,&QTimer::timeout,this,[this] { refresh(); });refresh_timer->start();
    auto help=new QLabel("The main shares its field, boost and optional server. Alts follow using their own Gathering settings.");
    help->setWordWrap(true);help->setObjectName("muted");layout->addWidget(help);
    connect(host_,&QPushButton::clicked,this,[this] { setup(true); });connect(join_,&QPushButton::clicked,this,[this] { setup(false); });
    connect(copy_,&QPushButton::clicked,this,[this] { QApplication::clipboard()->setText(controller_->invite()); });
    connect(retry_,&QPushButton::clicked,this,[this] { controller_->retry(); });
    connect(leave_,&QPushButton::clicked,this,[this] { controller_->leave(); });
    controller_->on_changed=[page=QPointer<Page>(this)] { if(page) page->refresh(); };refresh();
}
void Page::refresh() {
    status_->setText(controller_->status());
    const auto group=controller_->state()->snapshot();members_->setRowCount(int(group.peers.size()));
    const auto value=[](const coordination::Peer& peer,const char* topic,const char* key) -> QString {
        if (const auto t=peer.topics.find(topic);t!=peer.topics.end()) if(const auto v=t->second.find(key);v!=t->second.end())
            if (const auto s=std::get_if<std::string>(&v->second)) return QString::fromStdString(*s);
        return {};
    };
    for (int row=0;row<int(group.peers.size());++row) {
        const auto& peer=group.peers[row];auto field=value(peer,peer.host?"gathering.field":"support","field");
        if (field.isEmpty()) field=value(peer,"activity","field");
        const QStringList cells{QString::fromStdString(peer.name),peer.host?"Main":"Alt",field.isEmpty()?"—":field,value(peer,"activity","label")};
        for(int col=0;col<cells.size();++col) {
            auto* item=members_->item(row,col);if(!item) { item=new QTableWidgetItem;members_->setItem(row,col,item); }
            item->setText(cells[col]);item->setToolTip(cells[col]);
        }
    }
    members_->setFixedHeight(30*std::clamp(int(group.peers.size()),1,6)+members_->horizontalHeader()->height()+4);
    copy_->setVisible(!controller_->invite().isEmpty());retry_->setVisible(controller_->mapping_failed());
    leave_->setVisible(controller_->enabled() && controller_->joined());
    const bool connected=controller_->enabled() && controller_->joined();
    host_->setVisible(!connected);join_->setVisible(!connected);
    host_->setEnabled(controller_->enabled());join_->setEnabled(controller_->enabled());
    members_->setVisible(connected);
}
void Page::setup(bool host) {
    auto dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(host?"Host Alt Sync group":"Join Alt Sync group");
    auto layout=new QVBoxLayout(dialog);auto form=new QFormLayout;layout->addLayout(form);
    auto name=new QLineEdit(controller_->name());name->setMaxLength(32);form->addRow("Account name",name);
    auto address=new QLineEdit;address->setObjectName("accounts-address");address->setMaxLength(host?512:4096);
    address->setPlaceholderText(host?"Automatic":"Paste the main account’s invite");
    QRadioButton *local=nullptr,*internet=nullptr,*discord=nullptr;
    QLineEdit* server=nullptr;
    if(host) {
        auto networks=new QWidget;auto choices=new QHBoxLayout(networks);choices->setContentsMargins(0,0,0,0);
        local=new QRadioButton("Same network");local->setObjectName("accounts-local");local->setChecked(true);
        internet=new QRadioButton("Internet");internet->setObjectName("accounts-internet");
        discord=new QRadioButton("Discord");discord->setObjectName("accounts-discord");
        choices->addWidget(local);choices->addWidget(internet);choices->addWidget(discord);form->addRow("Connection",networks);
        server=new QLineEdit(controller_->suggested_server?controller_->suggested_server():QString{});
        server->setObjectName("accounts-server");server->setMaxLength(2048);server->setPlaceholderText("Private server link or Roblox user ID (optional)");
        form->addRow("Roblox server",server);
    } else form->addRow("Invite",address);
    auto channel=new QLineEdit;channel->setObjectName("accounts-discord-channel");channel->setMaxLength(20);
    channel->setPlaceholderText("Dedicated text channel ID");form->addRow("Discord channel",channel);form->setRowVisible(channel,false);
    auto token=new QLineEdit;token->setObjectName("accounts-discord-token");token->setMaxLength(256);token->setEchoMode(QLineEdit::Password);
    token->setPlaceholderText("Bot token — use the same bot on each account");form->addRow("Bot token",token);form->setRowVisible(token,false);
    QSpinBox* port=nullptr;QComboBox* adapter=nullptr;QToolButton* advanced=nullptr;QWidget* details=nullptr;
    QFormLayout* fields=nullptr;
    if(host) {
        advanced=new QToolButton;advanced->setText("Advanced");advanced->setCheckable(true);advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);advanced->setArrowType(Qt::RightArrow);layout->addWidget(advanced);
        details=new QWidget;fields=new QFormLayout(details);layout->addWidget(details);details->hide();
        port=new QSpinBox;port->setRange(1,65535);port->setValue(45831);fields->addRow("Port",port);fields->addRow("Reachable address",address);
        adapter=new QComboBox;adapter->setObjectName("accounts-network");adapter->addItem("Automatic",QString{});
        for(const auto& network:coordination::local_networks()) adapter->addItem(network.name+" · "+network.address.toString(),network.id);
        fields->addRow("Network",adapter);
        connect(advanced,&QToolButton::toggled,dialog,[dialog,details,advanced](bool open) {
            details->setVisible(open);advanced->setArrowType(open?Qt::DownArrow:Qt::RightArrow);dialog->adjustSize();
        });
    }
    auto hint=new QLabel;hint->setWordWrap(true);hint->setObjectName("accounts-hint");layout->addWidget(hint);
    const auto update=[=] {
        const bool use_discord=host?discord->isChecked():address->text().trimmed().startsWith("aurex://discord/");
        form->setRowVisible(channel,host && use_discord);form->setRowVisible(token,use_discord);
        if(host) {
            advanced->setVisible(!use_discord);details->setVisible(!use_discord && advanced->isChecked());
            fields->setRowVisible(address,internet->isChecked());fields->setRowVisible(adapter,local->isChecked() && adapter->count()>2);
        }
        hint->setText(use_discord?
            "Use one bot and a dedicated private text channel for the group. The bot needs View Channel, Read Message History and Send Messages. Updates arrive every few seconds; each account keeps one message updated. Bot tokens stay on each computer and are not included in invites.":
            !host?"Paste the main account’s invite. Your fallback setting decides what happens if the main stops or disconnects.":
            local->isChecked()?"Connect computers on the same Wi-Fi or Ethernet network. Copy the invite to each alt; no router forwarding is needed.":
            "Connect across the internet. Aurex requests a temporary router port while hosting. You can also supply a reachable address in Advanced.");
        if(dialog->isVisible())dialog->adjustSize();
    };
    if(host) { connect(local,&QRadioButton::toggled,dialog,update);connect(internet,&QRadioButton::toggled,dialog,update);connect(discord,&QRadioButton::toggled,dialog,update); }
    else connect(address,&QLineEdit::textChanged,dialog,update);
    update();
    auto error=new QLabel;error->setTextFormat(Qt::PlainText);error->setWordWrap(true);error->setMinimumHeight(error->fontMetrics().height()*2);layout->addWidget(error);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText(host?"Host":"Join");layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,dialog,[=,this] {
        try {
            if(host && discord->isChecked()) controller_->host_discord(name->text(),channel->text(),token->text(),server->text());
            else if(host) controller_->host(name->text(),address->text(),quint16(port->value()),server->text(),
                local->isChecked()?HostNetwork::local:HostNetwork::internet,adapter->currentData().toString());
            else controller_->join(name->text(),address->text(),token->text());
            dialog->accept();
        } catch(const std::exception& e) { error->setText(QString::fromUtf8(e.what())); }
    });
    dialog->setFixedWidth(520);dialog->adjustSize();dialog->show();
}
}
