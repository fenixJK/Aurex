#pragma once
#include "game/data/hive_preferences.hpp"
#include "game/detection/sprinklers.hpp"
#include <QButtonGroup>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <functional>

namespace hive::app {
class EquipmentSettings final : public QWidget {
    QSettings& settings_;
    QComboBox *equipment_, *interval_;
    QSpinBox* slot_;
    QButtonGroup* mode_;
    bool loading_{};
    void restore() {
        loading_=true;
        equipment_->setCurrentIndex(std::max(0,equipment_->findText(settings_.value("equipment/sprinkler","None").toString())));
        slot_->setValue(settings_.value("equipment/sprinklerSlot",1).toInt());
        mode_->button(std::clamp(settings_.value("conversion/mode",0).toInt(),0,2))->setChecked(true);
        interval_->setCurrentIndex(std::max(0,interval_->findData(settings_.value("conversion/minutes",30).toInt())));
        interval_->setEnabled(mode_->checkedId()==2);
        loading_=false;
    }
    void changed() {
        if(loading_) return;
        interval_->setEnabled(mode_->checkedId()==2);
        if(on_changed && !on_changed()) { restore(); return; }
        settings_.setValue("equipment/sprinkler",equipment_->currentText());
        settings_.setValue("equipment/sprinklerSlot",slot_->value());
        settings_.setValue("conversion/mode",mode_->checkedId());
        settings_.setValue("conversion/minutes",interval_->currentData());
    }
public:
    std::function<bool()> on_changed;
    explicit EquipmentSettings(QSettings& settings):settings_(settings) {
        auto layout=new QHBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(8);
        auto make_card=[&](const char* title) {
            auto widget=new QWidget; widget->setObjectName("card"); auto body=new QVBoxLayout(widget);
            body->setContentsMargins(10,10,10,10);
            auto heading=new QLabel(title); heading->setObjectName("title"); body->addWidget(heading); layout->addWidget(widget,1); return body;
        };
        auto sprinkler=make_card("Sprinklers");
        equipment_=new QComboBox; equipment_->setObjectName("core-sprinkler");
        for(const auto& item:game::sprinkler_equipment()) equipment_->addItem(QString::fromStdString(item.name));
        sprinkler->addWidget(equipment_);
        auto slot_row=new QHBoxLayout; slot_row->addWidget(new QLabel("Sprinkler slot"));
        slot_=new QSpinBox; slot_->setObjectName("core-sprinkler-slot"); slot_->setRange(1,7); slot_->setKeyboardTracking(false);
        slot_->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Fixed); slot_row->addWidget(slot_); slot_row->addStretch(); sprinkler->addLayout(slot_row);
        sprinkler->addStretch();
        auto balloon=make_card("Hive balloon");
        mode_=new QButtonGroup(this); auto modes=new QHBoxLayout; modes->setSpacing(4);
        const QStringList labels{"Off","Every return","Timed"};
        for(int i=0;i<labels.size();++i) { auto button=new QPushButton(labels[i]); button->setCheckable(true); mode_->addButton(button,i); modes->addWidget(button); }
        balloon->addLayout(modes);
        interval_=new QComboBox; interval_->setObjectName("core-balloon-interval");
        interval_->addItem("15 minutes",15); interval_->addItem("30 minutes",30);
        for(int i=1;i<=12;++i) interval_->addItem(QString::number(i)+(i==1?" hour":" hours"),i*60);
        balloon->addWidget(interval_);
        interval_->setToolTip("Convert when due on the next return to your hive."); balloon->addStretch();
        restore();
        connect(equipment_,&QComboBox::currentIndexChanged,this,[this] { changed(); });
        connect(slot_,&QSpinBox::valueChanged,this,[this] { changed(); });
        connect(interval_,&QComboBox::currentIndexChanged,this,[this] { changed(); });
        connect(mode_,&QButtonGroup::idClicked,this,[this] { changed(); });
    }
    game::SprinklerPreference sprinklers() const { return {equipment_->currentText().toStdString(),static_cast<unsigned>(slot_->value())}; }
    game::ConversionPreference conversion() const { return {mode_->checkedId()!=0,mode_->checkedId()==2?std::chrono::minutes(interval_->currentData().toInt()):Time{}}; }
};
}
