#include "presets.hpp"
#include <QDialog>
#include <QEvent>
#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <stdexcept>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTextBrowser>
#include <QJsonArray>
#include <QMenu>
#include <QToolButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

namespace hive {
namespace {
class PresetDialog final : public QDialog {
    void changeEvent(QEvent* event) override {
        QDialog::changeEvent(event);
        if (event->type()==QEvent::ActivationChange && isActiveWindow() && activated) activated();
    }
public:
    using QDialog::QDialog;
    std::function<void()> activated;
};
QJsonObject read(const QString& path) {
    QFile file(path); QJsonParseError error;
    if (!file.open(QIODevice::ReadOnly) || file.size()>1024*1024) throw std::runtime_error("Could not read preset");
    const auto document=QJsonDocument::fromJson(file.readAll(),&error); const auto preset=document.object();
    if (error.error!=QJsonParseError::NoError || preset["format"]!="aurex-preset" || preset["version"]!=QJsonValue(1)
        || !preset["modules"].isObject() || preset["name"].toString().trimmed().isEmpty() || preset["name"].toString().size()>80)
        throw std::runtime_error("Not an Aurex preset");
    return preset;
}
void write(const QString& path,const QJsonObject& preset) {
    const auto data=QJsonDocument(preset).toJson(); QSaveFile file(path);
    if (data.size()>1024*1024 || !file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit())
        throw std::runtime_error("Could not save preset");
}
}
void show_presets(QWidget* parent,const QString& folder,std::function<QJsonObject()> capture,
    std::function<void(const QJsonObject&)> apply,std::function<bool()> busy,std::function<std::vector<QJsonObject>()> starters) {
    for (auto existing:parent->findChildren<QDialog*>("preset-browser")) if (existing->isVisible()) {
        existing->raise(); existing->activateWindow(); return;
    }
    auto dialog=new PresetDialog(parent); dialog->setObjectName("preset-browser"); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Presets"); dialog->resize(700,490);
    auto layout=new QVBoxLayout(dialog); layout->setContentsMargins(16,16,16,16); layout->setSpacing(12);
    auto heading=new QLabel("Choose a setup"); heading->setObjectName("heading"); layout->addWidget(heading);
    auto hint=new QLabel("Preview a routine, or save your current settings for later."); hint->setObjectName("muted"); layout->addWidget(hint);
    auto body=new QHBoxLayout; body->setSpacing(16); layout->addLayout(body,1);
    auto list=new QListWidget; list->setObjectName("preset-list"); list->setMinimumWidth(180); list->setMaximumWidth(220); body->addWidget(list,1);
    auto description=new QTextBrowser; description->setObjectName("preset-preview"); description->setOpenLinks(false);
    description->setFrameShape(QFrame::NoFrame); description->setStyleSheet("background: transparent; border: none;"); body->addWidget(description,2);
    auto feedback=new QLabel; feedback->setObjectName("muted"); feedback->setWordWrap(true); feedback->hide(); layout->addWidget(feedback);
    auto buttons=new QHBoxLayout;
    auto save=new QPushButton("Save current…"),import=new QPushButton("Import…"),close=new QPushButton("Close"),load=new QPushButton("Apply");
    auto more=new QToolButton; more->setText("More"); more->setPopupMode(QToolButton::InstantPopup);
    auto menu=new QMenu(more); auto export_file=menu->addAction("Export selected…"),remove=menu->addAction("Delete selected…"); more->setMenu(menu);
    for (auto button:{save,import,close,load}) button->setAutoDefault(false);
    load->setObjectName("primary");
    buttons->addWidget(save); buttons->addWidget(import); buttons->addWidget(more); buttons->addStretch(); buttons->addWidget(close); buttons->addWidget(load); layout->addLayout(buttons);
    const auto refresh=[list,folder,starters](const QString& selected_name=QString{}) {
        const auto identity=[](const QListWidgetItem* item) {
            return item->data(Qt::UserRole).toString().isEmpty()?"builtin:"+item->text():item->data(Qt::UserRole).toString();
        };
        const auto previous=list->currentItem()?identity(list->currentItem()):QString{};
        const QSignalBlocker block(list); list->clear();
        const auto section=[list](const QString& name) {
            auto item=new QListWidgetItem(name,list); item->setFlags(Qt::NoItemFlags); item->setForeground(QColor("#9cacae"));
            auto font=item->font(); font.setBold(true); item->setFont(font);
        };
        const auto builtins=starters?starters():std::vector<QJsonObject>{};
        if (!builtins.empty()) section("Built-in");
        for (const auto& preset:builtins) {
            auto item=new QListWidgetItem(preset["name"].toString(),list); item->setSizeHint(QSize(0,36));
            item->setData(Qt::UserRole+1,preset);
        }
        bool saved_section=false;
        for (const auto& path:QDir(folder).entryInfoList({"*.json"},QDir::Files,QDir::Name)) {
            try {
                const auto preset=read(path.absoluteFilePath());
                if (!saved_section) { section("Saved"); saved_section=true; }
                auto item=new QListWidgetItem(preset["name"].toString(),list); item->setSizeHint(QSize(0,36));
                item->setData(Qt::UserRole,path.absoluteFilePath()); item->setData(Qt::UserRole+1,preset);
            } catch (...) { }
        }
        int first=-1,selected=-1;
        for (int i=0;i<list->count();++i) if (list->item(i)->flags()&Qt::ItemIsSelectable) {
            if (first<0) first=i;
            if (selected_name.isEmpty()?identity(list->item(i))==previous:list->item(i)->text()==selected_name) selected=i;
        }
        list->setCurrentRow(selected<0?first:selected);
    };
    const auto selected=[list,starters] {
        const auto item=list->currentItem();
        if (!item || !(item->flags()&Qt::ItemIsSelectable)) throw std::runtime_error("Choose a preset first");
        const auto path=item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) return read(path);
        // Built-ins keep the current selections, including edits made while this window was open.
        for (const auto& preset:starters()) if (preset["name"]==item->data(Qt::UserRole+1).toJsonObject()["name"]) return preset;
        throw std::runtime_error("This preset is no longer available. Close and reopen Presets.");
    };
    const auto preview=[=] {
        const auto item=list->currentItem();
        if (!item || !(item->flags()&Qt::ItemIsSelectable)) { description->clear(); return; }
        const auto preset=item->data(Qt::UserRole+1).toJsonObject();
        const bool builtin=item->data(Qt::UserRole).toString().isEmpty();
        QString html="<h2>"+preset["name"].toString().toHtmlEscaped()+"</h2>";
        const auto paragraph=[&](const QString& text) { if (!text.isEmpty()) html+="<p>"+text.toHtmlEscaped().replace("\n","<br>")+"</p>"; };
        const auto group=[&](const QString& label,const QJsonArray& values) {
            QStringList names; for (const auto& value:values) names.push_back(value.toString());
            if (!names.empty()) paragraph(label+": "+names.join(", "));
        };
        if (!builtin) paragraph("Restores the module settings, selections and enabled states saved in this setup.");
        else if (preset["defaultOrder"].toBool()) paragraph(preset["description"].toString());
        else paragraph("Keeps your fields, patterns, equipment and task selections.");
        group("On",preset["enabledModules"].toArray()); group("Off",preset["disabledModules"].toArray());
        if (builtin && !preset["routine"].toString().isEmpty()) paragraph("When work is due: "+preset["routine"].toString());
        const auto details=preset["details"].toArray();
        if (!details.empty()) {
            html+="<ul>"; for (const auto& detail:details) html+="<li>"+detail.toString().toHtmlEscaped()+"</li>"; html+="</ul>";
        } else if (builtin && !preset["defaultOrder"].toBool()) paragraph(preset["description"].toString());
        description->setHtml(html);
    };
    const auto state=[=] {
        const auto item=list->currentItem(); const bool chosen=item && (item->flags()&Qt::ItemIsSelectable);
        save->setEnabled(!busy()); load->setEnabled(chosen && !busy()); export_file->setEnabled(chosen);
        remove->setEnabled(chosen && !item->data(Qt::UserRole).toString().isEmpty());
        load->setToolTip(busy()?"Stop the macro before applying a preset":QString{});
    };
    const auto action=[feedback](const std::function<void()>& operation) {
        feedback->hide();
        try { operation(); }
        catch (const std::exception& error) { feedback->setText(QString::fromUtf8(error.what())); feedback->show(); }
    };
    const auto store=[folder](const QJsonObject& preset) {
        if (!QDir().mkpath(folder)) throw std::runtime_error("Could not create presets folder");
        write(QDir(folder).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)+".json"),preset);
    };
    QObject::connect(close,&QPushButton::clicked,dialog,&QDialog::reject);
    QObject::connect(save,&QPushButton::clicked,dialog,[=] {
        if (busy()) return;
        bool ok=false; const auto name=QInputDialog::getText(dialog,"Save preset","Name",QLineEdit::Normal,{},&ok).trimmed();
        if (!ok || name.isEmpty()) return;
        action([&] {
            if (busy()) throw std::runtime_error("Stop the macro before saving a preset");
            if (name.size()>80) throw std::runtime_error("Choose a shorter name");
            auto preset=capture(); preset["name"]=name; store(preset); refresh(name); preview(); state();
        });
    });
    QObject::connect(load,&QPushButton::clicked,dialog,[=] {
        if (!list->currentItem() || busy()) return;
        action([&] { apply(selected()); dialog->accept(); });
    });
    QObject::connect(import,&QPushButton::clicked,dialog,[=] {
        const auto path=QFileDialog::getOpenFileName(dialog,"Import preset",{},"Aurex preset (*.json)");
        if (!path.isEmpty()) action([&] { const auto preset=read(path); store(preset); refresh(preset["name"].toString()); preview(); state(); });
    });
    QObject::connect(export_file,&QAction::triggered,dialog,[=] {
        if (!list->currentItem()) return;
        const auto path=QFileDialog::getSaveFileName(dialog,"Export preset","aurex-preset.json","Aurex preset (*.json)");
        if (!path.isEmpty()) action([&] { write(path,selected()); });
    });
    QObject::connect(remove,&QAction::triggered,dialog,[=] {
        if (!list->currentItem()) return;
        const auto source=list->currentItem()->data(Qt::UserRole).toString(); if (source.isEmpty()) return;
        if (QMessageBox::question(dialog,"Delete preset","Delete “"+list->currentItem()->text()+"”?")!=QMessageBox::Yes) return;
        action([&] { if (!QFile::remove(source)) throw std::runtime_error("Could not delete preset"); refresh(); preview(); state(); });
    });
    auto timer=new QTimer(dialog); QObject::connect(timer,&QTimer::timeout,dialog,state); timer->start(250);
    QObject::connect(list,&QListWidget::itemSelectionChanged,dialog,[=] { preview(); state(); });
    dialog->activated=[=] { action([&] { refresh(); }); preview(); state(); };
    action([&] { refresh(); }); preview(); state(); dialog->show();
}
}
