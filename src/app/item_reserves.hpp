#pragma once
#include "game/data/item_stock.hpp"
#include "game/detection/item_menu.hpp"
#include <QDialog>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace hive::app {
inline QPushButton* item_reserves(QSettings& settings,std::shared_ptr<game::ItemStock> stock,QWidget* parent) {
    game::ItemAmounts initial;
    const auto saved=settings.value("items/reserves").toMap();
    for (auto it=saved.begin();it!=saved.end();++it) if (it.value().toLongLong()>0 && it.value().toLongLong()<=1000000000)
        initial[it.key().toStdString()]=it.value().toULongLong();
    stock->configure(std::move(initial));
    auto button=new QPushButton("Item reserves…",parent); button->setObjectName("item-reserves");
    QObject::connect(button,&QPushButton::clicked,button,[stock,&settings,button] {
        auto dialog=new QDialog(button); dialog->setWindowTitle("Item reserves"); dialog->setWindowModality(Qt::WindowModal); dialog->setAttribute(Qt::WA_DeleteOnClose);
        auto layout=new QVBoxLayout(dialog);
        auto help=new QLabel("Keep these amounts for later. Boosting, Blender and Wind Shrine share the same reserves.");
        help->setWordWrap(true); layout->addWidget(help);
        auto controls=new QHBoxLayout; auto item=new QComboBox;
        auto names=game::shrine_items(); names.push_back("Super Smoothie"); std::sort(names.begin(),names.end());
        for (const auto& name:names) item->addItem(QString::fromStdString(name));
        auto add=new QPushButton("Add"); controls->addWidget(item,1); controls->addWidget(add); layout->addLayout(controls);
        auto table=new QTableWidget(0,3); table->setHorizontalHeaderLabels({"Item","Keep at least","Inventory"}); table->verticalHeader()->hide();
        table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch); table->setColumnWidth(1,110); table->setColumnWidth(2,125);
        layout->addWidget(table,1);
        const auto save=[stock,&settings,table] {
            game::ItemAmounts amounts; QVariantMap values;
            for (int row=0;row<table->rowCount();++row) {
                const auto count=static_cast<QSpinBox*>(table->cellWidget(row,1))->value(); if (!count) continue;
                const auto name=table->item(row,0)->text(); amounts[name.toStdString()]=count; values[name]=count;
            }
            stock->configure(std::move(amounts)); settings.setValue("items/reserves",values);
        };
        const auto append=[table,save](const QString& name,int count) {
            for (int row=0;row<table->rowCount();++row) if (table->item(row,0)->text()==name) { table->selectRow(row); return; }
            const auto row=table->rowCount(); table->insertRow(row); table->setItem(row,0,new QTableWidgetItem(name));
            auto reserve=new QSpinBox; reserve->setRange(0,1000000000); reserve->setKeyboardTracking(false); reserve->setValue(count); table->setCellWidget(row,1,reserve);
            table->setItem(row,2,new QTableWidgetItem("Not read"));
            QObject::connect(reserve,&QSpinBox::valueChanged,table,[save] { save(); });
        };
        for (const auto& [name,count]:stock->read().reserves) append(QString::fromStdString(name),int(count));
        QObject::connect(add,&QPushButton::clicked,dialog,[append,save,item] { append(item->currentText(),1); save(); });
        auto footer=new QHBoxLayout; auto remove=new QPushButton("Remove"); auto done=new QPushButton("Done");
        footer->addWidget(remove); footer->addStretch(); footer->addWidget(done); layout->addLayout(footer);
        QObject::connect(remove,&QPushButton::clicked,dialog,[table,save] { if (table->currentRow()>=0) { table->removeRow(table->currentRow()); save(); } });
        QObject::connect(done,&QPushButton::clicked,dialog,&QDialog::accept);
        auto timer=new QTimer(dialog); timer->setInterval(1000);
        const auto refresh=[stock,table] {
            const auto state=stock->read();
            for (int row=0;row<table->rowCount();++row) {
                const auto found=state.counts.find(table->item(row,0)->text().toStdString());
                const auto text=found==state.counts.end()?QString("Not read"):QString::number(found->second.count)+(found->second.estimated?" estimated":" last seen");
                if (table->item(row,2)->text()!=text) table->item(row,2)->setText(text);
            }
        };
        QObject::connect(timer,&QTimer::timeout,dialog,refresh); refresh(); timer->start();
        dialog->setFixedSize(490,410); dialog->show();
    });
    return button;
}
}
