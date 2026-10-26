#include "setting_widgets.hpp"
#include <QTabBar>
#include <QPushButton>
#include <QMenu>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QCheckBox>
#include <QImageReader>
#include <QScrollArea>
#include <QWidgetAction>
#include <QListWidget>
#include <QLabel>
#include <QSignalBlocker>
#include <QButtonGroup>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <cmath>
#include <algorithm>

namespace hive {
namespace {
QString q(const std::string& value) { return QString::fromStdString(value); }
void set_number(QWidget* control,int value,const extensions::Setting& field) {
    if (auto* number=qobject_cast<QDoubleSpinBox*>(control)) number->setValue(value/std::pow(10.,field.decimals));
    else static_cast<QSpinBox*>(control)->setValue(value);
}
int read_number(QWidget* control,const extensions::Setting& field) {
    if (auto* number=qobject_cast<QDoubleSpinBox*>(control)) return int(std::round(number->value()*std::pow(10.,field.decimals)));
    return static_cast<QSpinBox*>(control)->value();
}
QLabel* text(const QString& value,const char* role) { auto label=new QLabel(value);label->setObjectName(role);label->setTextFormat(Qt::PlainText);return label; }
class ChoiceEditor final : public ChoiceControl {
    std::vector<std::string> choices_;
    QComboBox* menu_{};
    QSpinBox* number_{};
    QButtonGroup* buttons_{};
    void notify() { if(!signalsBlocked() && changed) changed(); }
public:
    ChoiceEditor(const extensions::Setting& field,const std::string& selected,bool compact):choices_(field.choices) {
        auto row=new QHBoxLayout(this); row->setContentsMargins(0,0,0,0); row->setSpacing(3);
        bool numbered=choices_.size()>1 && !choices_.front().empty()
            && !std::all_of(choices_.front().begin(),choices_.front().end(),[](unsigned char c) { return c>='0' && c<='9'; });
        for(std::size_t i=1;i<choices_.size();++i) numbered&=choices_[i]==std::to_string(i);
        int width=0; for(const auto& choice:choices_) width+=fontMetrics().horizontalAdvance(q(choice))+20;
        if(numbered) {
            number_=new QSpinBox; number_->setRange(0,int(choices_.size())-1); number_->setSpecialValueText(q(choices_.front()));
            number_->setKeyboardTracking(false); number_->setAccessibleName(q(field.label)); row->addWidget(number_); setFocusProxy(number_);
            connect(number_,&QSpinBox::valueChanged,this,[this] { notify(); });
        } else if(choices_.size()<=3 && width<=260) {
            buttons_=new QButtonGroup(this); buttons_->setExclusive(true);
            for(std::size_t i=0;i<choices_.size();++i) {
                auto button=new QPushButton(q(choices_[i])); button->setObjectName("choice-option"); button->setCheckable(true);
                button->setAutoDefault(false); buttons_->addButton(button,int(i)); row->addWidget(button);
                if(!i) setFocusProxy(button);
            }
            connect(buttons_,&QButtonGroup::idToggled,this,[this](int,bool checked) { if(checked) notify(); });
        } else {
            menu_=new QComboBox; for(const auto& choice:choices_) menu_->addItem(q(choice));
            if (compact) { menu_->setMinimumContentsLength(8); menu_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); }
            menu_->setAccessibleName(q(field.label)); row->addWidget(menu_); setFocusProxy(menu_);
            connect(menu_,&QComboBox::currentTextChanged,this,[this] { notify(); });
        }
        setValue(selected);
    }
    std::string value() const override {
        if(menu_) return menu_->currentText().toStdString();
        const int index=number_?number_->value():buttons_->checkedId();
        return index>=0 && index<int(choices_.size())?choices_[index]:std::string{};
    }
    void setValue(const std::string& value) override {
        const auto found=std::find(choices_.begin(),choices_.end(),value); if(found==choices_.end()) return;
        const int index=int(found-choices_.begin());
        if(menu_) menu_->setCurrentIndex(index);
        else if(number_) number_->setValue(index);
        else buttons_->button(index)->setChecked(true);
    }
};
class CollectionControl final : public CollectionEditor {
    QTabBar* fields_;
    QPushButton* add_;
    QMenu* available_;
    QLabel* empty_;
    QPushButton* defaults_;
    QStackedWidget* pages_;
    std::vector<std::string> choices_;
    std::map<std::string,QWidget*> editors_;
    int addField(const std::string& choice) {
        const int index=fields_->addTab(q(choice));
        auto close=new QToolButton; close->setText("×"); close->setObjectName("collection-remove");
        close->setAccessibleName("Remove "+q(choice)); close->setToolTip(close->accessibleName());
        fields_->setTabButton(index,QTabBar::RightSide,close);
        connect(close,&QToolButton::clicked,this,[this,close] {
            for(int i=0;i<fields_->count();++i) if(fields_->tabButton(i,QTabBar::RightSide)==close) { fields_->removeTab(i); refresh(true); break; }
        });
        return index;
    }
    void refresh(bool edited=false) {
        const auto selected=value(); available_->clear();
        for(const auto& choice:choices_) if(std::find(selected.begin(),selected.end(),choice)==selected.end()) {
            auto action=available_->addAction(q(choice));
            connect(action,&QAction::triggered,this,[this,choice] {
                fields_->setCurrentIndex(addField(choice)); refresh(true);
            });
        }
        add_->setEnabled(!available_->isEmpty());
        if(fields_->currentIndex()>=0) pages_->setCurrentWidget(editors_.at(fields_->tabText(fields_->currentIndex()).toStdString()));
        pages_->setVisible(fields_->count()>0); empty_->setVisible(fields_->count()==0); defaults_->setEnabled(fields_->count()>0);
        if(edited && !signalsBlocked() && changed) changed();
    }
public:
    CollectionControl(const std::vector<std::string>& choices,const SettingSelection& selected,const std::string& label):choices_(choices) {
        auto layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(8);
        auto bar=new QHBoxLayout; bar->setSpacing(6); layout->addLayout(bar);
        fields_=new QTabBar; fields_->setObjectName("collection-fields"); fields_->setExpanding(false);
        fields_->setMovable(true); fields_->setDrawBase(false); fields_->setUsesScrollButtons(true);
        fields_->setToolTip("Drag to change the order."); bar->addWidget(fields_,1);
        add_=new QPushButton; add_->setText("+ "+q(label)); add_->setObjectName("collection-add");
        available_=new QMenu(add_); add_->setMinimumWidth(88); bar->addWidget(add_);
        connect(add_,&QPushButton::clicked,this,[this] { available_->popup(add_->mapToGlobal(QPoint(0,add_->height()))); });
        auto reset=new QPushButton("Defaults"); defaults_=reset; reset->setObjectName("collection-defaults"); bar->addWidget(reset);
        pages_=new QStackedWidget; pages_->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum); layout->addWidget(pages_);
        empty_=text("Use “+ "+q(label)+"” to add an item.","muted"); layout->addWidget(empty_);
        for(const auto& choice:choices) {
            auto panel=new QWidget; auto sections=new QVBoxLayout(panel); sections->setContentsMargins(0,0,0,0); sections->setSpacing(8); sections->setAlignment(Qt::AlignTop);
            editors_[choice]=panel; pages_->addWidget(panel);
        }
        connect(fields_,&QTabBar::currentChanged,this,[this] { refresh(); if(this->selected) this->selected(); });
        connect(fields_,&QTabBar::tabMoved,this,[this] { refresh(true); });
        connect(fields_,&QTabBar::tabCloseRequested,this,[this](int index) { fields_->removeTab(index); refresh(true); });
        connect(reset,&QPushButton::clicked,this,[this] { if(fields_->currentIndex()>=0 && restore) restore(fields_->tabText(fields_->currentIndex()).toStdString()); });
        setValue(selected);
    }
    std::string current() const override { return fields_->tabText(fields_->currentIndex()).toStdString(); }
    void select(const std::string& choice) override {
        for(int i=0;i<fields_->count();++i) if(fields_->tabText(i)==q(choice)) {fields_->setCurrentIndex(i);break;}
    }
    void addSection(const std::string& choice,QWidget* section) override {
        auto layout=static_cast<QVBoxLayout*>(editors_.at(choice)->layout());
        layout->addWidget(section);
    }
    SettingSelection value() const override {
        SettingSelection result; for(int i=0;i<fields_->count();++i) result.push_back(fields_->tabText(i).toStdString()); return result;
    }
    void setValue(const SettingSelection& selected) override {
        const auto before=value();
        QSignalBlocker block(fields_); while(fields_->count()) fields_->removeTab(0);
        for(const auto& choice:selected) if(editors_.contains(choice)) addField(choice);
        if(fields_->count()) fields_->setCurrentIndex(0); refresh(value()!=before);
        if(this->selected) this->selected();
    }
};
class InlineSelection final : public QWidget, public SelectionValue {
    std::vector<std::pair<std::string,QAbstractButton*>> choices_;
    SettingSelection order_;
    bool ordered_;
public:
    InlineSelection(const extensions::Setting& field,const SettingSelection& selected,bool compact,unsigned columns=0):ordered_(field.ordered) {
        const auto& choices=field.choices; const bool ordered=field.ordered;
        auto layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(4);
        auto content=new QWidget; auto grid=new QGridLayout(content); grid->setContentsMargins(0,0,0,0);
        grid->setHorizontalSpacing(field.icons.empty()?18:6); grid->setVerticalSpacing(field.icons.empty()?(compact?2:8):6);
        const auto add=[&](const std::string& choice,QGridLayout* target,int index,int columns) {
            QAbstractButton* check;
            if (const auto icon=field.icons.find(choice);icon!=field.icons.end()) {
                auto tile=new QToolButton; tile->setCheckable(true); tile->setObjectName("image-choice");
                const QSize size{38,28}; QImageReader image(q(icon->second));
                image.setScaledSize(image.size().scaled(size*2,Qt::KeepAspectRatio));
                tile->setIcon(QPixmap::fromImage(image.read())); tile->setIconSize(size); tile->setFixedSize(50,38);
                tile->setToolTip(q(choice)); tile->setAccessibleName(q(choice));
                tile->setFocusPolicy(Qt::StrongFocus); check=tile;
            } else {
                check=new QCheckBox(q(choice)); check->setObjectName(ordered?"choice-tile":"compact-choice"); check->setProperty("compact",compact);
                check->ensurePolished(); check->setMinimumSize(check->sizeHint());
            }
            target->addWidget(check,index/columns,index%columns); choices_.emplace_back(choice,check);
            connect(check,&QAbstractButton::toggled,this,[this,choice](bool enabled) {
                std::erase(order_,choice); if(enabled) order_.push_back(choice);
                if(changed) changed();
            });
        };
        if (field.groups.empty()) {
            const int count=columns?int(columns):!field.icons.empty()?9:choices.size()>24 && std::all_of(choices.begin(),choices.end(),[](const auto& name) { return name.size()<=12; })?4:
                choices.size()>=3 && std::all_of(choices.begin(),choices.end(),[](const auto& name) { return name.size()<=20; })?3:2;
            for (std::size_t i=0;i<choices.size();++i) add(choices[i],grid,int(i),count);
            if (field.icons.empty()) for (int column=0;column<count;++column) grid->setColumnStretch(column,1);
            else grid->setColumnStretch(count,1);
        } else {
            for (std::size_t i=0;i<field.groups.size();++i) {
                const auto& group=field.groups[i]; auto panel=new QWidget;
                auto items=new QGridLayout(panel); items->setContentsMargins(0,0,0,0); items->setHorizontalSpacing(8); items->setVerticalSpacing(0);
                items->addWidget(text(q(group.label),"section-heading"),0,0,1,2);
                for (std::size_t j=0;j<group.choices.size();++j) add(group.choices[j],items,int(j)+2,2);
                items->setColumnStretch(0,1); items->setColumnStretch(1,1);
                grid->addWidget(panel,int(i)/2,int(i)%2,Qt::AlignTop);
            }
            grid->setColumnStretch(0,1); grid->setColumnStretch(1,1);
        }
        layout->addWidget(content);
        if(!ordered && choices.size()>4) {
            auto buttons=new QHBoxLayout; buttons->setSpacing(8);
            auto all=new QToolButton; all->setText("All"); auto none=new QToolButton; none->setText("None"); buttons->addWidget(all); buttons->addWidget(none); buttons->addStretch(); layout->addLayout(buttons);
            connect(all,&QToolButton::clicked,this,[this,choices] { setValue(choices); });
            connect(none,&QToolButton::clicked,this,[this] { setValue({}); });
        }
        setValue(selected);
    }
    SettingSelection value() const override {
        if(ordered_) return order_;
        SettingSelection selected; for(const auto& [name,check]:choices_) if(check->isChecked()) selected.push_back(name); return selected;
    }
    void setValue(const SettingSelection& selected) override {
        order_.clear();
        for(const auto& name:selected) if(std::any_of(choices_.begin(),choices_.end(),[&](const auto& choice) { return choice.first==name; })) order_.push_back(name);
        for(const auto& [name,check]:choices_) { QSignalBlocker block(check); check->setChecked(std::find(order_.begin(),order_.end(),name)!=order_.end()); }
        if(changed) changed();
    }
};
class SelectionControl final : public QPushButton, public SelectionValue {
    std::vector<std::pair<std::string,QCheckBox*>> items_;
    QListWidget* ordered_{};
    void refresh() {
        const auto selected=value();
        setText(selected.empty()?"None selected":QString::number(selected.size())+" selected");
        QStringList names; for (const auto& name : selected) names.push_back(q(name)); setToolTip(names.join(", "));
        if(changed) changed();
    }
public:
    SelectionControl(const std::vector<std::string>& choices,const SettingSelection& selected,bool ordered) {
        auto menu=new QMenu(this); setMenu(menu);
        auto contents=new QWidget; auto layout=new QVBoxLayout(contents); layout->setContentsMargins(10,8,10,8);
        auto actions=new QHBoxLayout; auto all=new QPushButton("All"),none=new QPushButton("None");
        actions->addWidget(all); actions->addWidget(none); layout->addLayout(actions);
        if (ordered) {
            ordered_=new QListWidget; ordered_->setObjectName("selection-order");
            ordered_->setDragDropMode(QAbstractItemView::InternalMove);
            ordered_->setFixedSize(260,std::min(360,static_cast<int>(choices.size())*30+12));
            for (const auto& choice : choices) {
                auto item=new QListWidgetItem(q(choice),ordered_); item->setCheckState(Qt::Unchecked);
            }
            layout->addWidget(ordered_);
            auto moves=new QHBoxLayout;
            for (const auto direction : {-1,1}) {
                auto move=new QPushButton(direction<0?"Move up":"Move down"); moves->addWidget(move);
                connect(move,&QPushButton::clicked,this,[this,direction] {
                    const auto row=ordered_->currentRow(),next=row+direction;
                    if (row<0 || next<0 || next>=ordered_->count()) return;
                    auto item=ordered_->takeItem(row); ordered_->insertItem(next,item); ordered_->setCurrentItem(item); refresh();
                });
            }
            layout->addLayout(moves);
            connect(ordered_,&QListWidget::itemChanged,this,[this] { refresh(); });
            connect(ordered_->model(),&QAbstractItemModel::rowsMoved,this,[this] { refresh(); });
        } else {
            auto scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
            auto list=new QWidget; auto rows=new QVBoxLayout(list); rows->setContentsMargins(0,0,0,0);
            for (const auto& choice : choices) {
                auto box=new QCheckBox(q(choice)); rows->addWidget(box); items_.emplace_back(choice,box);
                connect(box,&QCheckBox::toggled,this,[this] { refresh(); });
            }
            scroll->setWidget(list); scroll->setFixedSize(260,std::min(360,static_cast<int>(choices.size())*30+12)); layout->addWidget(scroll);
        }
        auto widget=new QWidgetAction(menu); widget->setDefaultWidget(contents); menu->addAction(widget);
        connect(all,&QPushButton::clicked,this,[this,choices] {
            if (ordered_) { for (int i=0;i<ordered_->count();++i) ordered_->item(i)->setCheckState(Qt::Checked); }
            else setValue(choices);
        });
        connect(none,&QPushButton::clicked,this,[this] { setValue({}); }); setValue(selected);
    }
    SettingSelection value() const {
        if (ordered_) {
            SettingSelection result;
            for (int i=0;i<ordered_->count();++i) if (ordered_->item(i)->checkState()==Qt::Checked)
                result.push_back(ordered_->item(i)->text().toStdString());
            return result;
        }
        SettingSelection result; for (const auto& [name,box] : items_) if (box->isChecked()) result.push_back(name); return result;
    }
    void setValue(const SettingSelection& selected) {
        if (ordered_) {
            QSignalBlocker blocked(ordered_);
            for (int i=0;i<ordered_->count();++i) ordered_->item(i)->setCheckState(Qt::Unchecked);
            int next=0;
            for (const auto& name : selected) {
                const auto found=ordered_->findItems(q(name),Qt::MatchExactly);
                if (found.empty()) continue;
                auto item=ordered_->takeItem(ordered_->row(found.front()));
                ordered_->insertItem(next++,item); item->setCheckState(Qt::Checked);
            }
        }
        for (const auto& [name,box] : items_) { QSignalBlocker blocked(box); box->setChecked(std::find(selected.begin(),selected.end(),name)!=selected.end()); }
        refresh();
    }
};
}
CollectionEditor* collection_control(const std::vector<std::string>& choices,const SettingSelection& selected,const std::string& label) { return new CollectionControl(choices,selected,label); }
ChoiceControl* choice_control(const extensions::Setting& field,const std::string& selected,bool compact) { return new ChoiceEditor(field,selected,compact); }
QWidget* inline_selection(const extensions::Setting& field,const SettingSelection& selected,bool compact,unsigned columns) { return new InlineSelection(field,selected,compact,columns); }
QWidget* selection_control(const std::vector<std::string>& choices,const SettingSelection& selected,bool ordered) { return new SelectionControl(choices,selected,ordered); }
extensions::Value read_setting_value(QWidget* control,const extensions::Setting& field) {
    if (std::holds_alternative<bool>(field.initial)) return static_cast<QCheckBox*>(control)->isChecked();
    if (std::holds_alternative<int>(field.initial)) return read_number(control,field);
    if (std::holds_alternative<std::string>(field.initial)) return static_cast<ChoiceControl*>(control)->value();
    return dynamic_cast<SelectionValue*>(control)->value();
}
void write_setting_value(QWidget* control,const extensions::Setting& field,const extensions::Value& value) {
    if (const auto* v=std::get_if<bool>(&value)) static_cast<QCheckBox*>(control)->setChecked(*v);
    else if (const auto* v=std::get_if<int>(&value)) {
        if (auto* number=qobject_cast<QDoubleSpinBox*>(control)) {
            const auto divisor=std::pow(10.,field.decimals); number->setRange(field.minimum/divisor,field.maximum/divisor);
            number->setSpecialValueText(q(field.minimum_label));
        } else {
            auto* integer=static_cast<QSpinBox*>(control); integer->setRange(field.minimum,field.maximum);
            integer->setSpecialValueText(q(field.minimum_label));
        }
        set_number(control,*v,field);
    } else if (const auto* v=std::get_if<std::string>(&value)) static_cast<ChoiceControl*>(control)->setValue(*v);
    else dynamic_cast<SelectionValue*>(control)->setValue(std::get<SettingSelection>(value));
}
void update_setting_conditions(std::span<const extensions::Setting> schema,const std::map<std::string,QWidget*>& controls,
    const extensions::Settings& baseline,bool busy) {
    const auto checked=[&](const std::string& id) {
        return controls.contains(id)?static_cast<QCheckBox*>(controls.at(id))->isChecked():std::get<bool>(baseline.at(id));
    };
    for (const auto& field:schema) if (!field.enabled_by.empty() && controls.contains(field.id)) {
        bool active;
        if (field.enabled_values.empty()) active=checked(field.enabled_by);
        else {
            const auto value=controls.contains(field.enabled_by)?static_cast<ChoiceControl*>(controls.at(field.enabled_by))->value():std::get<std::string>(baseline.at(field.enabled_by));
            active=std::find(field.enabled_values.begin(),field.enabled_values.end(),value)!=field.enabled_values.end();
            if(!field.dependent_labels.empty()) {
                const auto label=field.dependent_labels.find(value);
                auto* check=static_cast<QCheckBox*>(controls.at(field.id));
                check->setText(q(label==field.dependent_labels.end()?field.label:label->second));
                check->setAccessibleName(check->text());
            }
        }
        controls.at(field.id)->setEnabled(!busy && active);
    }
    for (const auto& field:schema) if ((field.step!=1 || !field.step_toggle.empty()) && controls.contains(field.id)) {
        const int step=!field.step_toggle.empty() && checked(field.step_toggle)?field.checked_step:field.step;
        auto* control=controls.at(field.id); const QSignalBlocker block(control);
        const int minimum=int((static_cast<long long>(field.minimum)+step-1)/step*step),maximum=field.maximum/step*step;
        const int value=std::clamp(read_number(control,field)/step*step,minimum,maximum);
        if (auto* number=qobject_cast<QDoubleSpinBox*>(control)) {
            const auto divisor=std::pow(10.,field.decimals);
            number->setRange(minimum/divisor,maximum/divisor); number->setSingleStep(step/divisor);
        } else {
            auto* integer=static_cast<QSpinBox*>(control); integer->setRange(minimum,maximum); integer->setSingleStep(step);
        }
        set_number(control,value,field);
    }
}

}
