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

namespace hive::features::accounts {
Page::Page(std::shared_ptr<Controller> controller,QWidget* parent)
    :QWidget(parent),controller_(std::move(controller)) {
    auto layout=new QVBoxLayout(this);layout->setContentsMargins(0,8,0,0);layout->setSpacing(12);
    auto actions=new QHBoxLayout;host_=new QPushButton("Host group…");join_=new QPushButton("Join group…");
    copy_=new QPushButton("Copy invite");leave_=new QPushButton("Leave group");retry_=new QPushButton("Try again");retry_->setObjectName("accounts-retry");
    host_->setObjectName("accounts-host");join_->setObjectName("accounts-join");copy_->setObjectName("accounts-copy");leave_->setObjectName("accounts-leave");
    actions->addWidget(host_);actions->addWidget(join_);actions->addStretch();actions->addWidget(retry_);actions->addWidget(copy_);actions->addWidget(leave_);layout->addLayout(actions);
    status_=new QLabel;status_->setObjectName("accounts-status");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);layout->addWidget(status_);
    members_=new QLabel;members_->setObjectName("accounts-members");members_->setTextFormat(Qt::PlainText);members_->setAlignment(Qt::AlignTop);layout->addWidget(members_);
    auto help=new QLabel("Host shares a server and field. Join follows with this account’s own settings.");
    help->setWordWrap(true);help->setObjectName("muted");layout->addWidget(help);
    connect(host_,&QPushButton::clicked,this,[this] { setup(true); });connect(join_,&QPushButton::clicked,this,[this] { setup(false); });
    connect(copy_,&QPushButton::clicked,this,[this] { QApplication::clipboard()->setText(controller_->invite()); });
    connect(retry_,&QPushButton::clicked,this,[this] { controller_->retry(); });
    connect(leave_,&QPushButton::clicked,this,[this] { controller_->leave(); });
    controller_->on_changed=[page=QPointer<Page>(this)] { if(page) page->refresh(); };refresh();
}
void Page::refresh() {
    status_->setText(controller_->status());QStringList names;
    for(const auto& peer:controller_->state()->snapshot().peers) {
        auto line=QString::fromStdString(peer.name)+(peer.host?" · Host":"");
        if(const auto topic=peer.topics.find("activity");topic!=peer.topics.end()) {
            if(const auto entry=topic->second.find("label");entry!=topic->second.end())
                if(const auto label=std::get_if<std::string>(&entry->second);label && !label->empty()) line+=" · "+QString::fromStdString(*label);
        }
        names.append(line);
    }
    members_->setText(names.join('\n'));copy_->setVisible(!controller_->invite().isEmpty());retry_->setVisible(controller_->mapping_failed());
    leave_->setVisible(controller_->enabled() && controller_->joined());
    const bool connected=controller_->enabled() && controller_->joined();
    host_->setVisible(!connected);join_->setVisible(!connected);
    host_->setEnabled(controller_->enabled());join_->setEnabled(controller_->enabled());
    members_->setVisible(connected);
}
void Page::setup(bool host) {
    auto dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(host?"Host account group":"Join account group");auto layout=new QVBoxLayout(dialog);auto form=new QFormLayout;layout->addLayout(form);
    auto name=new QLineEdit(controller_->name());name->setMaxLength(32);form->addRow("Account name",name);
    QRadioButton* local=nullptr;
    if(host) {
        auto networks=new QWidget;auto choices=new QHBoxLayout(networks);choices->setContentsMargins(0,0,0,0);
        local=new QRadioButton("Same network");local->setObjectName("accounts-local");local->setChecked(true);
        auto internet=new QRadioButton("Internet");internet->setObjectName("accounts-internet");
        choices->addWidget(local);choices->addWidget(internet);choices->addStretch();form->addRow("Connection",networks);
    }
    auto address=new QLineEdit;address->setObjectName("accounts-address");address->setMaxLength(host?512:2048);
    address->setPlaceholderText(host?"Automatic":"Paste the host's invite");if(!host) form->addRow("Invite",address);
    QLineEdit* server=nullptr;
    if(host) {
        server=new QLineEdit(controller_->suggested_server?controller_->suggested_server():QString{});
        server->setObjectName("accounts-server");server->setMaxLength(2048);server->setPlaceholderText("Private server link or Roblox user ID (optional)");
        form->addRow("Roblox server",server);
    }
    auto port=new QSpinBox;port->setRange(1,65535);port->setValue(45831);
    auto hint=new QLabel;hint->setWordWrap(true);hint->setObjectName("accounts-hint");
    QComboBox* adapter=nullptr;
    if(host) {
        auto advanced=new QToolButton;advanced->setText("Advanced");advanced->setCheckable(true);advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);advanced->setArrowType(Qt::RightArrow);layout->addWidget(advanced);
        auto details=new QWidget;auto fields=new QFormLayout(details);fields->addRow("Port",port);fields->addRow("Reachable address",address);layout->addWidget(details);details->hide();
        adapter=new QComboBox;adapter->setObjectName("accounts-network");adapter->addItem("Automatic",QString{});
        for(const auto& network:coordination::local_networks()) adapter->addItem(network.name+" · "+network.address.toString(),network.id);
        fields->addRow("Network",adapter);
        address->setToolTip("Use an address reachable by guests if you have already set up forwarding. This skips automatic router setup.");
        connect(advanced,&QToolButton::toggled,dialog,[dialog,details,advanced](bool open) {
            details->setVisible(open);advanced->setArrowType(open?Qt::DownArrow:Qt::RightArrow);
            dialog->setFixedHeight(dialog->sizeHint().height());
        });
        const auto update_network=[dialog,local,fields,address,adapter,hint] {
            fields->setRowVisible(address,!local->isChecked());fields->setRowVisible(adapter,local->isChecked() && adapter->count()>2);
            hint->setText(local->isChecked()?
                "Connect computers on the same Wi-Fi or Ethernet network. Copy the invite to each guest; no router forwarding is needed.":
                "Connect across the internet. Aurex requests a temporary router port while hosting. Your router must support forwarding, or you can supply a reachable address in Advanced.");
            if(dialog->isVisible()) dialog->setFixedHeight(dialog->sizeHint().height());
        };
        connect(local,&QRadioButton::toggled,dialog,update_network);update_network();
    } else { delete port;port=nullptr; }
    if(!host) hint->setText("For a same-network group, connect to the host's Wi-Fi or Ethernet network. Paste their invite to join. Your macro continues on its own if the host disconnects.");
    layout->addWidget(hint);auto error=new QLabel;error->setTextFormat(Qt::PlainText);error->setWordWrap(true);error->setMinimumHeight(error->fontMetrics().height()*2);layout->addWidget(error);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText(host?"Host":"Join");layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,dialog,[this,dialog,host,name,address,port,server,local,adapter,error] {
        try {
            if(host) controller_->host(name->text(),address->text(),quint16(port->value()),server->text(),
                local->isChecked()?HostNetwork::local:HostNetwork::internet,adapter->currentData().toString());
            else controller_->join(name->text(),address->text());
            dialog->accept();
        } catch(const std::exception& e) { error->setText(QString::fromUtf8(e.what())); }
    });
    dialog->setFixedWidth(450);dialog->adjustSize();dialog->setFixedHeight(dialog->sizeHint().height());dialog->show();
}
}
