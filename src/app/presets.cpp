#include "presets.hpp"
#include <QDialog>
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
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

namespace hive {
namespace {
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
    std::function<void(const QJsonObject&)> apply,std::function<bool()> busy,std::vector<QJsonObject> starters) {
    auto dialog=new QDialog(parent); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Presets"); dialog->setWindowModality(Qt::WindowModal); dialog->setFixedSize(520,390);
    auto layout=new QVBoxLayout(dialog);
    auto hint=new QLabel("Choose a starting point or a saved setup. Starting points change run order and gather interrupts; your fields, equipment and selected tasks stay as they are.");
    hint->setWordWrap(true); layout->addWidget(hint);
    auto list=new QListWidget; layout->addWidget(list,1);
    auto description=new QLabel; description->setWordWrap(true); description->setMinimumHeight(60); layout->addWidget(description);
    auto buttons=new QHBoxLayout;
    auto save=new QPushButton("Save as…"),load=new QPushButton("Apply"),import=new QPushButton("Import…"),export_file=new QPushButton("Export…"),remove=new QPushButton("Delete");
    for (auto button:{save,load,import,export_file,remove}) buttons->addWidget(button);
    layout->addLayout(buttons);
    const auto refresh=[list,folder,starters] {
        list->clear();
        for (const auto& preset:starters) {
            auto item=new QListWidgetItem(preset["name"].toString()+" · Starting point",list);
            item->setData(Qt::UserRole+1,preset);
        }
        for (const auto& path:QDir(folder).entryInfoList({"*.json"},QDir::Files,QDir::Name)) {
            try {
                const auto preset=read(path.absoluteFilePath()); auto item=new QListWidgetItem(preset["name"].toString(),list);
                item->setData(Qt::UserRole,path.absoluteFilePath());
            } catch (...) { }
        }
        if (list->count()) list->setCurrentRow(0);
    };
    const auto selected=[list] {
        const auto item=list->currentItem();
        return item->data(Qt::UserRole+1).isValid()?item->data(Qt::UserRole+1).toJsonObject():read(item->data(Qt::UserRole).toString());
    };
    const auto action=[dialog](const std::function<void()>& operation) {
        try { operation(); }
        catch (const std::exception& error) { QMessageBox::warning(dialog,"Presets",QString::fromUtf8(error.what())); }
    };
    const auto store=[folder](const QJsonObject& preset) {
        if (!QDir().mkpath(folder)) throw std::runtime_error("Could not create presets folder");
        write(QDir(folder).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)+".json"),preset);
    };
    QObject::connect(save,&QPushButton::clicked,dialog,[=] {
        if (busy()) return;
        bool ok=false; const auto name=QInputDialog::getText(dialog,"Save preset","Name",QLineEdit::Normal,{},&ok).trimmed();
        if (!ok || name.isEmpty()) return;
        action([&] {
            if (busy()) throw std::runtime_error("Stop the macro before saving a preset");
            if (name.size()>80) throw std::runtime_error("Choose a shorter name");
            auto preset=capture(); preset["name"]=name; store(preset); refresh();
        });
    });
    QObject::connect(load,&QPushButton::clicked,dialog,[=] {
        if (!list->currentItem() || busy()) return;
        action([&] { apply(selected()); dialog->accept(); });
    });
    QObject::connect(import,&QPushButton::clicked,dialog,[=] {
        const auto path=QFileDialog::getOpenFileName(dialog,"Import preset",{},"Aurex preset (*.json)");
        if (!path.isEmpty()) action([&] { store(read(path)); refresh(); });
    });
    QObject::connect(export_file,&QPushButton::clicked,dialog,[=] {
        if (!list->currentItem()) return;
        const auto path=QFileDialog::getSaveFileName(dialog,"Export preset","aurex-preset.json","Aurex preset (*.json)");
        if (!path.isEmpty()) action([&] { write(path,selected()); });
    });
    QObject::connect(remove,&QPushButton::clicked,dialog,[=] {
        if (!list->currentItem() || list->currentItem()->data(Qt::UserRole+1).isValid()) return;
        const auto source=list->currentItem()->data(Qt::UserRole).toString();
        if (QMessageBox::question(dialog,"Delete preset","Delete “"+list->currentItem()->text()+"”?")!=QMessageBox::Yes) return;
        action([&] { if (!QFile::remove(source)) throw std::runtime_error("Could not delete preset"); refresh(); });
    });
    const auto state=[=] {
        const auto item=list->currentItem(); const bool builtin=item && item->data(Qt::UserRole+1).isValid();
        save->setEnabled(!busy()); load->setEnabled(item && !busy()); export_file->setEnabled(item); remove->setEnabled(item && !builtin);
        description->setText(builtin?item->data(Qt::UserRole+1).toJsonObject()["description"].toString():QString{});
    };
    auto timer=new QTimer(dialog); QObject::connect(timer,&QTimer::timeout,dialog,state); timer->start(250);
    QObject::connect(list,&QListWidget::itemSelectionChanged,dialog,state);
    refresh(); state(); dialog->open();
}
}
