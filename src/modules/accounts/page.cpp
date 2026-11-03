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
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <algorithm>

namespace hive::features::accounts {
namespace {
QLabel* label(const QString& text,const char* name="muted") {
    auto result=new QLabel(text);result->setObjectName(name);result->setTextFormat(Qt::PlainText);result->setWordWrap(true);return result;
}
}
Page::Page(std::shared_ptr<Controller> controller,QWidget* parent,const std::map<std::string,QWidget*>& sections)
    :QWidget(parent),controller_(std::move(controller)) {
    setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
    auto layout=new QVBoxLayout(this);layout->setContentsMargins(0,4,0,0);layout->setSpacing(10);
    layout->setAlignment(Qt::AlignTop);
    choose_role_=new QWidget;auto choose=new QVBoxLayout(choose_role_);choose->setContentsMargins(0,0,0,0);choose->setSpacing(12);
    choose->addWidget(label("Which account is this?","section-heading"));
    auto cards=new QHBoxLayout;cards->setSpacing(12);choose->addLayout(cards);
    const auto card=[&](const QString& title,const QString& description) {
        auto panel=new QWidget;panel->setObjectName("card");auto body=new QVBoxLayout(panel);body->setContentsMargins(14,14,14,14);body->setSpacing(10);
        body->addWidget(label(title,"heading"));body->addWidget(label(description),1);
        auto button=new QPushButton("Set up "+title.toLower()+"…");body->addWidget(button);cards->addWidget(panel,1);return button;
    };
    host_=card("Main account","Lead the group. Share your gathering field, boosts and server with your alts.");
    join_=card("Alt account","Follow a main account. Connect using the invite copied from its Alt Sync page.");
    host_->setObjectName("accounts-host");join_->setObjectName("accounts-join");
    choose->addWidget(label("Set up the main first, then paste its invite on each alt. Connections work across Mac and Windows."));layout->addWidget(choose_role_);
    connected_=new QWidget;auto connection=new QVBoxLayout(connected_);connection->setContentsMargins(0,0,0,0);connection->setSpacing(4);
    connection->setAlignment(Qt::AlignTop);
    auto actions=new QHBoxLayout;role_=label({},"heading");role_->setObjectName("accounts-role");actions->addWidget(role_,1);
    copy_=new QPushButton("Copy invite");leave_=new QPushButton("Leave group");retry_=new QPushButton("Try again");
    copy_->setObjectName("accounts-copy");leave_->setObjectName("accounts-leave");retry_->setObjectName("accounts-retry");
    actions->addWidget(retry_);actions->addWidget(copy_);actions->addWidget(leave_);connection->addLayout(actions);
    role_help_=label({});connection->addWidget(role_help_);layout->addWidget(connected_);
    status_=label({},"accounts-status");layout->addWidget(status_);
    tabs_=new QTabWidget;tabs_->setObjectName("accounts-tabs");layout->addWidget(tabs_);
    auto settings=new QWidget;auto settings_layout=new QVBoxLayout(settings);settings_layout->setContentsMargins(0,8,0,0);settings_layout->setSpacing(12);
    settings_layout->setAlignment(Qt::AlignTop);
    for(const auto* id:{"following","tasks","allowed","shared"}) if(const auto section=sections.find(id);section!=sections.end()) settings_layout->addWidget(section->second);
    if (sections.contains("following") && sections.contains("tasks") && sections.contains("allowed")) {
        if (auto mode=sections.at("following")->findChild<QComboBox*>()) {
            const auto update=[mode,tasks=sections.at("tasks"),allowed=sections.at("allowed")] {
                const bool following=mode->currentText()!="Own field rotation";
                tasks->setEnabled(following);allowed->setEnabled(following);
            };
            connect(mode,&QComboBox::currentTextChanged,this,update);update();
        }
    }
    settings_layout->addWidget(label("Uses this alt's Gathering patterns. For items during the main's boost, choose a Main's boost rule in Boosting → Hotbar."));
    tabs_->addTab(settings,"Alt settings");
    auto group=new QWidget;auto group_layout=new QVBoxLayout(group);group_layout->setContentsMargins(0,8,0,0);group_layout->setSpacing(8);
    group_layout->setAlignment(Qt::AlignTop);
    boost_=label({},"accounts-main-boost");group_layout->addWidget(boost_);
    members_=new QTableWidget(0,5);members_->setObjectName("accounts-members");
    members_->setHorizontalHeaderLabels({"Account","Role","Current field","Following","Activity"});
    members_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);members_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    members_->verticalHeader()->hide();members_->verticalHeader()->setDefaultSectionSize(30);
    members_->setEditTriggers(QAbstractItemView::NoEditTriggers);members_->setSelectionMode(QAbstractItemView::NoSelection);
    members_->setShowGrid(false);members_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);group_layout->addWidget(members_);
    tabs_->addTab(group,"Group status");
    connect(tabs_,&QTabWidget::currentChanged,this,[this] { QTimer::singleShot(0,this,[this] { fit_tabs(); }); });
    auto refresh_timer=new QTimer(this);refresh_timer->setInterval(1000);connect(refresh_timer,&QTimer::timeout,this,[this] { refresh(); });refresh_timer->start();
    connect(host_,&QPushButton::clicked,this,[this] { setup(true); });connect(join_,&QPushButton::clicked,this,[this] { setup(false); });
    connect(copy_,&QPushButton::clicked,this,[this] { QApplication::clipboard()->setText(controller_->invite()); });
    connect(retry_,&QPushButton::clicked,this,[this] { controller_->retry(); });
    connect(leave_,&QPushButton::clicked,this,[this] { controller_->leave(); });
    controller_->on_changed=[page=QPointer<Page>(this)] { if(page) page->refresh(); };refresh();
}
void Page::refresh() {
    status_->setText(controller_->status());
    const auto now=monotonic_now();
    const auto main=controller_->main_boost()->read(now);
    if (main) {
        const auto seconds=std::chrono::duration_cast<std::chrono::seconds>(main->expires-now).count();
        boost_->setText(QString("Main’s boost · %1 · %2:%3 remaining").arg(QString::fromStdString(main->field))
            .arg(std::max<std::int64_t>(0,seconds)/60).arg(std::max<std::int64_t>(0,seconds)%60,2,10,QChar('0')));
    } else boost_->setText("Main’s boost · No confirmed active boost");
    const auto group=controller_->state()->snapshot();members_->setRowCount(int(group.peers.size()));
    const auto value=[](const coordination::Peer& peer,const char* topic,const char* key) -> QString {
        if (const auto t=peer.topics.find(topic);t!=peer.topics.end()) if(const auto v=t->second.find(key);v!=t->second.end())
            if (const auto s=std::get_if<std::string>(&v->second)) return QString::fromStdString(*s);
        return {};
    };
    for (int row=0;row<int(group.peers.size());++row) {
        const auto& peer=group.peers[row];const auto field=value(peer,"activity","field");
        const auto following=peer.host?QString{}:value(peer,"support","field");
        const QStringList cells{QString::fromStdString(peer.name),peer.host?"Main":"Alt",field.isEmpty()?"—":field,
            following.isEmpty()?"—":following,value(peer,"activity","label")};
        for(int col=0;col<cells.size();++col) {
            auto* item=members_->item(row,col);if(!item) { item=new QTableWidgetItem;members_->setItem(row,col,item); }
            item->setText(cells[col]);item->setToolTip(cells[col]);
        }
    }
    members_->setFixedHeight(30*std::clamp(int(group.peers.size()),1,6)+members_->horizontalHeader()->height()+4);
    const auto role=controller_->role();const bool is_main=role==coordination::Role::host;
    const bool connected=controller_->enabled() && controller_->joined() && role!=coordination::Role::offline;
    choose_role_->setVisible(!connected);connected_->setVisible(connected);tabs_->setVisible(connected);
    role_->setText(QString("%1 · %2").arg(is_main?"Main account":"Alt account",controller_->name()));
    role_help_->setText(is_main?"Your field and boosts lead this group. Copy the invite to connect your alts.":"Choose how this alt gathers and which other tasks it runs.");
    tabs_->setTabVisible(0,!is_main);tabs_->tabBar()->setVisible(!is_main);
    if(role!=shown_role_) { tabs_->setCurrentIndex(is_main?1:0);shown_role_=role; }
    copy_->setVisible(is_main && !controller_->invite().isEmpty());retry_->setVisible(controller_->mapping_failed());
    host_->setEnabled(controller_->enabled());join_->setEnabled(controller_->enabled());
    fit_tabs();
}
void Page::fit_tabs() {
    auto current=tabs_->currentWidget();if(!current) return;
    current->layout()->activate();
    tabs_->setFixedHeight(current->sizeHint().height()+(tabs_->tabBar()->isHidden()?0:tabs_->tabBar()->sizeHint().height())+6);
}
void Page::setup(bool host) {
    auto dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(host?"Set up main account":"Connect alt account");
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
            !host?"Paste the invite from your main account’s Alt Sync page. After connecting, choose how this alt follows.":
            local->isChecked()?"Connect computers on the same Wi-Fi or Ethernet network. Copy the invite to each alt; no router forwarding is needed.":
            "Connect across the internet. Aurex requests a temporary router port while hosting. You can also supply a reachable address in Advanced.");
        if(dialog->isVisible())dialog->adjustSize();
    };
    if(host) { connect(local,&QRadioButton::toggled,dialog,update);connect(internet,&QRadioButton::toggled,dialog,update);connect(discord,&QRadioButton::toggled,dialog,update); }
    else connect(address,&QLineEdit::textChanged,dialog,update);
    update();
    auto error=new QLabel;error->setTextFormat(Qt::PlainText);error->setWordWrap(true);error->setMinimumHeight(error->fontMetrics().height()*2);layout->addWidget(error);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText(host?"Create group":"Connect");layout->addWidget(buttons);
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
