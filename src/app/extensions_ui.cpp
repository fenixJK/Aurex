#include "extensions_ui.hpp"
#include "command_form.hpp"
#include "module_plot.hpp"
#include "module_metrics.hpp"
#include "navigation.hpp"
#include "ordered_rows.hpp"
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QScrollArea>
#include <QFileDialog>
#include <QSignalBlocker>
#include <QMenu>
#include <QWidgetAction>
#include <QListWidget>
#include <QMessageBox>
#include <QSettings>
#include "local_data.hpp"
#include <set>
#include <algorithm>
#include <cmath>

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
QLabel* text(const QString& value,const char* role=nullptr) {
    auto label=new QLabel(value); label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
    if (role) label->setObjectName(role); return label;
}
struct SelectionValue {
    std::function<void()> changed;
    virtual ~SelectionValue()=default;
    virtual SettingSelection value() const=0;
    virtual void setValue(const SettingSelection&)=0;
};
class CollectionControl final : public QWidget, public SelectionValue {
    QTabBar* fields_;
    QPushButton* add_;
    QMenu* available_;
    QStackedWidget* pages_;
    std::vector<std::string> choices_;
    std::map<std::string,QWidget*> editors_;
    int addField(const std::string& choice) {
        const int index=fields_->addTab(q(choice));
        auto close=new QToolButton; close->setText("×"); close->setObjectName("collection-remove");
        close->setAccessibleName("Remove "+q(choice)); close->setToolTip(close->accessibleName());
        fields_->setTabButton(index,QTabBar::RightSide,close);
        connect(close,&QToolButton::clicked,this,[this,close] {
            for(int i=0;i<fields_->count();++i) if(fields_->tabButton(i,QTabBar::RightSide)==close) { fields_->removeTab(i); refresh(); break; }
        });
        return index;
    }
    void refresh() {
        const auto selected=value(); available_->clear();
        for(const auto& choice:choices_) if(std::find(selected.begin(),selected.end(),choice)==selected.end()) {
            auto action=available_->addAction(q(choice));
            connect(action,&QAction::triggered,this,[this,choice] {
                fields_->setCurrentIndex(addField(choice)); refresh();
            });
        }
        add_->setEnabled(!available_->isEmpty());
        if(fields_->currentIndex()>=0) pages_->setCurrentWidget(editors_.at(fields_->tabText(fields_->currentIndex()).toStdString()));
        pages_->setVisible(fields_->count()>0);
        if(changed) changed();
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
        auto reset=new QPushButton("Defaults"); reset->setObjectName("collection-defaults"); bar->addWidget(reset);
        pages_=new QStackedWidget; layout->addWidget(pages_);
        for(const auto& choice:choices) {
            auto panel=new QWidget; auto sections=new QVBoxLayout(panel); sections->setContentsMargins(0,0,0,0); sections->setSpacing(8);
            editors_[choice]=panel; pages_->addWidget(panel);
        }
        connect(fields_,&QTabBar::currentChanged,this,[this] { refresh(); });
        connect(fields_,&QTabBar::tabMoved,this,[this] { refresh(); });
        connect(fields_,&QTabBar::tabCloseRequested,this,[this](int index) { fields_->removeTab(index); refresh(); });
        connect(reset,&QPushButton::clicked,this,[this] { if(fields_->currentIndex()>=0 && restore) restore(fields_->tabText(fields_->currentIndex()).toStdString()); });
        setValue(selected);
    }
    std::function<void(const std::string&)> restore;
    void addSection(const std::string& choice,const std::string& title,QWidget* section) {
        auto layout=static_cast<QVBoxLayout*>(editors_.at(choice)->layout());
        layout->addWidget(section);
    }
    SettingSelection value() const override {
        SettingSelection result; for(int i=0;i<fields_->count();++i) result.push_back(fields_->tabText(i).toStdString()); return result;
    }
    void setValue(const SettingSelection& selected) override {
        QSignalBlocker block(fields_); while(fields_->count()) fields_->removeTab(0);
        for(const auto& choice:selected) if(editors_.contains(choice)) addField(choice);
        if(fields_->count()) fields_->setCurrentIndex(0); refresh();
    }
};
class InlineSelection final : public QWidget, public SelectionValue {
    std::vector<std::pair<std::string,QCheckBox*>> choices_;
    SettingSelection order_;
    bool ordered_;
public:
    InlineSelection(const extensions::Setting& field,const SettingSelection& selected,bool compact):ordered_(field.ordered) {
        const auto& choices=field.choices; const bool ordered=field.ordered;
        auto layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(4);
        auto content=new QWidget; auto grid=new QGridLayout(content); grid->setContentsMargins(0,0,0,0);
        grid->setHorizontalSpacing(18); grid->setVerticalSpacing(compact?2:8);
        const auto add=[&](const std::string& choice,QGridLayout* target,int index,int columns) {
            auto check=new QCheckBox(q(choice)); check->setObjectName(ordered?"choice-tile":"compact-choice"); check->setProperty("compact",compact);
            target->addWidget(check,index/columns,index%columns); choices_.emplace_back(choice,check);
            connect(check,&QCheckBox::toggled,this,[this,choice](bool enabled) {
                std::erase(order_,choice); if(enabled) order_.push_back(choice);
                if(changed) changed();
            });
        };
        if (field.groups.empty()) {
            const int columns=choices.size()>24 && std::all_of(choices.begin(),choices.end(),[](const auto& name) { return name.size()<=12; })?4:
                choices.size()>=6 && std::all_of(choices.begin(),choices.end(),[](const auto& name) { return name.size()<=20; })?3:2;
            for (std::size_t i=0;i<choices.size();++i) add(choices[i],grid,int(i),columns);
            for (int column=0;column<columns;++column) grid->setColumnStretch(column,1);
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
        if(!ordered) {
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
struct ExtensionsUi::Page {
    QWidget* widget{};
    QWidget* tool{};
    QWidget* content{};
    QWidget* row{};
    QLabel* feedback{};
    QLabel* error{};
    QPushButton* enabled{};
    extensions::Settings baseline;
    QLabel* status{};
    ModulePlot* plot{};
    ModuleMetrics* metrics{};
    bool active{};
    std::optional<bool> busy;
    std::map<std::string,QWidget*> controls;
    std::vector<OrderedRows*> ordered_rows;
    void update_conditions(const extensions::Manifest& manifest,bool busy) {
        for (const auto& field:manifest.settings) if (!field.enabled_by.empty() && controls.contains(field.id))
            controls.at(field.id)->setEnabled(!busy && static_cast<QCheckBox*>(controls.at(field.enabled_by))->isChecked());
    }
    extensions::Settings read(const extensions::Manifest& manifest) const {
        extensions::Settings result=baseline;
        for (const auto& field : manifest.settings) {
            if(field.hidden && !controls.contains(field.id)) continue;
            auto* control=controls.at(field.id);
            if (std::holds_alternative<bool>(field.initial)) result[field.id]=static_cast<QCheckBox*>(control)->isChecked();
            else if (std::holds_alternative<int>(field.initial)) result[field.id]=read_number(control,field);
            else if (std::holds_alternative<std::string>(field.initial)) result[field.id]=static_cast<QComboBox*>(control)->currentText().toStdString();
            else result[field.id]=dynamic_cast<SelectionValue*>(control)->value();
        }
        return result;
    }

};
ExtensionsUi::ExtensionsUi(Navigation* navigation,QStackedWidget* pages,QString bundled,QString user_root,QString state_file,std::function<bool()> busy,RegisterProviders register_providers,extensions::Host::FailureHandler failure_handler,ToolFactory tool_factory,std::function<void(automation::Launch)> launch,TargetPicker picker,PageFactory page_factory)
    : QObject(pages),host_(state_file,busy,std::move(failure_handler)),navigation_(navigation),pages_(pages),roots_{std::move(bundled),user_root},user_root_(std::move(user_root)),busy_(std::move(busy)),tool_factory_(std::move(tool_factory)),page_factory_(std::move(page_factory)),launch_(std::move(launch)),picker_(std::move(picker)),order_file_(state_file+".order.json") {
    QSettings order_settings(order_file_,app::json_settings_format());
    for (const auto& id:order_settings.value("modules/order").toStringList()) order_.push_back(id.toStdString());
    if (order_settings.status()!=QSettings::NoError) throw std::runtime_error("Could not read the module run order.");
    host_.set_command_launcher(launch_);
    if (register_providers) register_providers(host_);
    auto scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    manager_=scroll;
    auto contents=new QWidget; scroll->setWidget(contents);
    auto layout=new QVBoxLayout(contents); layout->setContentsMargins(12,10,12,10); layout->setSpacing(8);
    auto heading=new QHBoxLayout; heading->addWidget(text("Modules","heading"),1);
    layout->addLayout(heading);
    layout->addWidget(text("Enable the features you want to run. Use their sidebar tabs for settings; Start macro runs them together.","muted"));
    message_=text({},"muted"); layout->addWidget(message_);
    auto order_label=text("Run order · drag to change","muted"); layout->addWidget(order_label);
    order_tabs_=new QTabBar; order_tabs_->setMovable(true); order_tabs_->setExpanding(false); order_tabs_->setDrawBase(false);
    order_tabs_->setToolTip("Higher modules run first when multiple are ready. Gather interrupt overrides this order and can stop active gathering."); layout->addWidget(order_tabs_);
    connect(order_tabs_,&QTabBar::tabMoved,this,[this] {
        if (busy_()) return;
        std::vector<std::string> moved;
        for (int i=0;i<order_tabs_->count();++i) moved.push_back(order_tabs_->tabData(i).toString().toStdString());
        perform([&] { set_run_order(std::move(moved)); });
    });
    auto sections=new QWidget; list_=new QVBoxLayout(sections); list_->setContentsMargins(0,0,0,0); list_->setSpacing(6); list_->addStretch(); layout->addWidget(sections);
    auto tools=new QWidget; auto toolbar=new QHBoxLayout(tools); toolbar->setContentsMargins(0,0,0,0);
    install_=new QPushButton("Install module"); reload_=new QPushButton("Reload modules");
    toolbar->addWidget(install_); toolbar->addWidget(reload_); toolbar->addStretch(); layout->addWidget(tools);
    install_->setToolTip("Install a trusted Lua extension package.");
    layout->addStretch(); pages_->addWidget(manager_); navigation_->addItem("Modules"); navigation_->setModuleStart(navigation_->count());
    connect(reload_,&QPushButton::clicked,this,[this] {
        if(busy_()) return;
        reload_->setText("Reloading…"); reload_->setEnabled(false);
        QTimer::singleShot(0,this,[this] {
            const bool loaded=perform([this] { apply_settings(); scan(); }); reload_->setText("Reload modules"); reload_->setEnabled(!busy_());
            if(loaded && host_.diagnostics().empty()) message_->setText("Packages reloaded.");
        });
    });
    connect(install_,&QPushButton::clicked,this,[this] {
        if (busy_()) return;
        auto source=QFileDialog::getExistingDirectory(manager_,"Choose extension package folder"); if (source.isEmpty() || busy_()) return;
        const bool installed=perform([&] {
            auto manifest=extensions::read_manifest(source+"/extension.json");
            if (host_.entries().contains(manifest.id)) throw std::runtime_error("An extension with this ID is already installed");
            host_.install(source,user_root_);
        });
        if (installed) scan();
    });
    scan();
}
ExtensionsUi::~ExtensionsUi() = default;
void ExtensionsUi::replace_providers(const RegisterProviders& registration) {
    if (busy_()) throw std::logic_error("Stop the session before changing module services");
    host_.replace_providers(registration);
    scan();
}
bool ExtensionsUi::perform(const std::function<void()>& action) {
    bool success=true;
    try { action(); message_->clear(); } catch (const std::exception& e) { message_->setText(e.what()); success=false; }
    reconcile(); refresh_list(); return success;
}
void ExtensionsUi::scan() {
    const auto selected=pages_->currentWidget()?pages_->currentWidget()->property("feature-id").toString():QString{};
    for (auto& [id,page] : extension_pages_) {
        const auto index=pages_->indexOf(page->widget); pages_->removeWidget(page->widget); navigation_->removeItem(index);
        list_->removeWidget(page->row); delete page->row; delete page->widget;
    }
    extension_pages_.clear();
    host_.discover(roots_); reconcile(); refresh_list();
    if (extension_pages_.contains(selected.toStdString())) navigation_->setCurrentRow(pages_->indexOf(extension_pages_.at(selected.toStdString())->widget));
    QStringList errors; for (const auto& message : host_.diagnostics()) errors.append(q(message)); message_->setText(errors.join("\n"));
}
void ExtensionsUi::refresh_list() {
    for (const auto& [id,page] : extension_pages_) {
        const auto& entry=host_.entries().at(id);
        QSignalBlocker blocked(page->enabled); page->enabled->setChecked(entry.requested); page->enabled->setText(entry.requested?"Disable":"Enable");
        page->enabled->setEnabled(!busy_());
        page->status->setText(!entry.issue.empty()?"Setup needed":entry.active()?"Enabled":"Off");
        page->status->setToolTip(q(entry.issue)); page->error->setText(q(entry.issue)); page->error->setVisible(!entry.issue.empty());
    }
    order_tabs_->setEnabled(!busy_());
    if (busy_()) return;
    try {
        auto entries=host_.routine_entries();
        std::stable_sort(entries.begin(),entries.end(),[&](const auto& a,const auto& b) {
            const auto rank=[&](const auto& entry) {
                const auto found=std::find(order_.begin(),order_.end(),entry.id);
                return found==order_.end()?order_.size()+std::size_t(100-entry.participation.priority):std::size_t(found-order_.begin());
            };
            return rank(a)<rank(b);
        });
        for (const auto& entry:entries) if (std::find(order_.begin(),order_.end(),entry.id)==order_.end()) order_.push_back(entry.id);
        bool changed=order_tabs_->count()!=static_cast<int>(entries.size());
        for (int i=0;!changed && i<order_tabs_->count();++i) changed=order_tabs_->tabData(i).toString()!=q(entries[i].id);
        if (changed) {
            QSignalBlocker block(order_tabs_);
            while(order_tabs_->count()) order_tabs_->removeTab(0);
            for (const auto& entry:entries) {
                const int tab=order_tabs_->addTab(q(host_.entries().at(entry.id).manifest.name));
                order_tabs_->setTabData(tab,q(entry.id));
            }
        }
        order_tabs_->setVisible(order_tabs_->count()>1);
    } catch (const std::exception&) { order_tabs_->hide(); }
}
void ExtensionsUi::reconcile() {
    const auto selected=pages_->currentWidget()?pages_->currentWidget()->property("feature-id").toString():QString{};
    std::map<std::string,extensions::Settings> drafts;
    for (auto it=extension_pages_.begin();it!=extension_pages_.end();) {
        const auto found=host_.entries().find(it->first);
        if (found==host_.entries().end() || found->second.active()!=it->second->active) {
            if (found!=host_.entries().end()) drafts.emplace(it->first,it->second->read(found->second.manifest));
            const auto index=pages_->indexOf(it->second->widget); pages_->removeWidget(it->second->widget); navigation_->removeItem(index);
            list_->removeWidget(it->second->row); it->second->row->hide(); it->second->row->deleteLater();
            it->second->widget->hide(); it->second->widget->deleteLater(); it=extension_pages_.erase(it);
        } else ++it;
    }
    for (const auto& [id,entry] : host_.entries()) {
        if (extension_pages_.contains(id)) continue;
        const auto& saved_values=drafts.contains(id)?drafts.at(id):entry.settings;
        auto page=std::make_unique<Page>(); page->active=entry.active(); page->baseline=saved_values;
        page->row=new QWidget; page->row->setObjectName("feature-card"); page->row->setProperty("feature-id",q(id));
        auto header=new QHBoxLayout(page->row); header->setContentsMargins(10,8,10,8);
        auto description=new QVBoxLayout; description->addWidget(text(q(entry.manifest.name),"title")); header->addLayout(description,1);
        page->enabled=new QPushButton(entry.requested?"Disable":"Enable"); page->enabled->setCheckable(true); page->enabled->setObjectName("feature-enabled"); page->enabled->setChecked(entry.requested);
        page->status=text({},"muted"); description->addWidget(page->status); header->addWidget(page->enabled);
        auto open=new QPushButton("Settings"); header->addWidget(open);
        connect(open,&QPushButton::clicked,this,[this,id] { navigation_->setCurrentRow(pages_->indexOf(extension_pages_.at(id)->widget)); });
        auto scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        page->widget=scroll; scroll->setObjectName("feature-settings"); scroll->setProperty("feature-id",q(id));
        auto body=new QWidget; scroll->setWidget(body);
        auto layout=new QVBoxLayout(body); layout->setContentsMargins(12,10,12,10); layout->setSpacing(8);
        auto title=new QHBoxLayout; title->addWidget(text(q(entry.manifest.name),"heading"),1);
        if (!entry.active()) {
            auto enable=new QPushButton("Enable "+q(entry.manifest.name)); title->addWidget(enable);
            connect(enable,&QPushButton::clicked,page->enabled,&QPushButton::click);
        }
        layout->addLayout(title);
        page->feedback=text({},"muted"); page->feedback->hide(); layout->addWidget(page->feedback);
        connect(page->enabled,&QPushButton::toggled,this,[this,id](bool on) {
            if (busy_()) { refresh_list(); return; }
            perform([this,id,on] { enable_module(id,on); });
        });
        if(page_factory_) if(auto* content=page_factory_(id,body)) { page->content=content;layout->addWidget(content); }
        const bool dense=entry.manifest.page && entry.manifest.page->compact;
        auto sections=entry.manifest.page?entry.manifest.page->sections:std::vector<extensions::PageSection>{};
        if (!entry.manifest.page) {
            sections.push_back({"overview","",{},{},true,3});
            if (!entry.manifest.settings.empty()) {
                extensions::PageSection settings{"settings","Settings"};
                for (const auto& field : entry.manifest.settings) settings.settings.push_back(field.id);
                sections.push_back(std::move(settings));
            }
        }
        for(auto& section:sections) std::erase_if(section.settings,[&settings=entry.manifest.settings](const auto& id) {
            const auto field=std::find_if(settings.begin(),settings.end(),[&](const auto& f) { return f.id==id; });
            return field->hidden;
        });
        std::erase_if(sections,[](const auto& section) { return section.settings.empty() && !section.metrics && section.description.empty() && section.tool.empty(); });
        std::map<std::string,CollectionControl*> collections;
        for (const auto& section:sections) if(!section.collection.empty() && !collections.contains(section.collection)) {
            const auto& field=*std::find_if(entry.manifest.settings.begin(),entry.manifest.settings.end(),[&](const auto& f) { return f.id==section.collection; });
            auto editor=new CollectionControl(field.choices,std::get<SettingSelection>(saved_values.at(field.id)),field.label);
            collections[field.id]=editor;
            editor->restore=[this,id,collection=field.id](const std::string& choice) {
                const auto& manifest=host_.entries().at(id).manifest;
                auto& controls=extension_pages_.at(id)->controls;
                for(const auto& section:manifest.page->sections) if(section.collection==collection && section.choice==choice)
                    for(const auto& key:section.settings) {
                        const auto& field=*std::find_if(manifest.settings.begin(),manifest.settings.end(),[&](const auto& f) { return f.id==key; });
                        if(field.hidden) continue;
                        auto* control=controls.at(key);
                        if(const auto* v=std::get_if<bool>(&field.initial)) static_cast<QCheckBox*>(control)->setChecked(*v);
                        else if(const auto* v=std::get_if<int>(&field.initial)) set_number(control,*v,field);
                        else if(const auto* v=std::get_if<std::string>(&field.initial)) static_cast<QComboBox*>(control)->setCurrentText(q(*v));
                        else dynamic_cast<SelectionValue*>(control)->setValue(std::get<SettingSelection>(field.initial));
                    }
            };
        }
        std::map<std::string,QVBoxLayout*> tools;
        std::set<std::pair<std::string,QVBoxLayout*>> tool_links;
        auto tool_buttons=new QHBoxLayout;
        const auto tool_panel=[&](const std::string& name,bool live=false,QVBoxLayout* destination=nullptr) -> QVBoxLayout* {
            QVBoxLayout* body{}; QDialog* dialog{};
            if(tools.contains(name)) { body=tools.at(name); dialog=static_cast<QDialog*>(body->parentWidget()); }
            else {
                dialog=new QDialog(page->widget->window(),Qt::Sheet); dialog->setWindowTitle(q(name)); dialog->setWindowModality(Qt::WindowModal); dialog->setSizeGripEnabled(false); dialog->setFixedSize(480,260);
                connect(page->widget,&QObject::destroyed,dialog,&QObject::deleteLater);
                body=new QVBoxLayout(dialog); body->setContentsMargins(16,16,16,16); body->setSpacing(12); tools[name]=body;
            }
            if (tool_links.insert({name,destination}).second) {
                auto button=new QPushButton(q(name)+"…"); button->setProperty("live-tool",live);
                if (destination) destination->addWidget(button,0,Qt::AlignLeft); else tool_buttons->addWidget(button);
                connect(button,&QPushButton::clicked,dialog,[dialog] { dialog->show(); dialog->raise(); });
            }
            return body;
        };
        std::map<std::string,QVBoxLayout*> tabs;
        std::map<std::string,QHBoxLayout*> tab_actions;
        QTabWidget* page_tabs{};
        if(std::any_of(sections.begin(),sections.end(),[](const auto& section) { return !section.tab.empty(); })) {
            page_tabs=new QTabWidget; page_tabs->setObjectName("module-tabs");
            page_tabs->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Ignored);
        }
        for(const auto& section:sections) if(!section.tab.empty() && !tabs.contains(section.tab)) {
            auto panel=new QWidget; auto body=new QVBoxLayout(panel); body->setContentsMargins(0,0,0,0); body->setSpacing(6);
            auto scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidget(panel); tabs[section.tab]=body;
            auto tab=new QWidget; auto content=new QVBoxLayout(tab); content->setContentsMargins(0,0,0,0); content->setSpacing(6);
            content->addWidget(scroll,1); tab_actions[section.tab]=new QHBoxLayout; content->addLayout(tab_actions[section.tab]);
            page_tabs->addTab(tab,q(section.tab));
        }
        std::map<std::string,OrderedRows*> ordered_groups;
        std::set<std::string> row_headings;
        std::map<std::string,int> row_widths;
        for (const auto& section:sections) if (section.row && section.order_group.empty())
            row_widths[section.tab]=std::max(row_widths[section.tab],pages_->fontMetrics().horizontalAdvance(q(section.title)));
        for (const auto& section : sections) {
            if (!section.tab.empty() && layout->indexOf(page_tabs)<0) layout->addWidget(page_tabs,1);
            if (!section.tool.empty() && section.settings.empty() && section.description.empty() && !section.metrics) {
                tool_panel(section.tool,false,section.tab.empty()?nullptr:tabs.at(section.tab)); continue;
            }
            const bool compact=section.settings.size()==1 && !section.collapsed && section.collection.empty()
                && std::holds_alternative<bool>(saved_values.at(section.settings.front()));
            const bool selection_only=section.settings.size()==1 && std::any_of(entry.manifest.settings.begin(),entry.manifest.settings.end(),[&](const auto& field) { return field.id==section.settings.front() && field.inline_choices; });
            auto group=new QWidget; group->setObjectName("module-section-"+q(section.id)); group->setAccessibleName(q(section.title));
            OrderedRows* ordered{};
            QSpinBox* priority{};
            if(!section.order_group.empty()) {
                const auto key=section.tab+"/"+section.order_group;
                if(!ordered_groups.contains(key)) {
                    ordered=new OrderedRows; ordered_groups[key]=ordered; page->ordered_rows.push_back(ordered);
                    auto destination=section.tab.empty()?layout:tabs.at(section.tab);
                    auto headings=new QGridLayout; headings->setHorizontalSpacing(8);
                    headings->addWidget(text(q(section.tab.empty()?section.order_group:section.tab),"muted"),0,0);
                    headings->setColumnStretch(0,1);
                    for(std::size_t i=0;i<section.settings.size();++i) {
                        const auto& field=*std::find_if(entry.manifest.settings.begin(),entry.manifest.settings.end(),[&](const auto& f) { return f.id==section.settings[i]; });
                        headings->addWidget(text(q(field.label),"muted"),0,int(i)+1); headings->setColumnStretch(int(i)+1,1);
                    }
                    destination->addLayout(headings); destination->addWidget(ordered);
                } else ordered=ordered_groups.at(key);
                const auto& field=*std::find_if(entry.manifest.settings.begin(),entry.manifest.settings.end(),[&](const auto& f) { return f.id==section.order_setting; });
                priority=new QSpinBox(group); priority->setObjectName(q(field.id)); priority->setRange(field.minimum,field.maximum); priority->setValue(std::get<int>(saved_values.at(field.id))); priority->hide();
                page->controls[field.id]=priority;
            }
            auto section_layout=new QVBoxLayout(group); section_layout->setContentsMargins(0,0,0,0); section_layout->setSpacing(8);
            if (section.collection.empty() && section.collapsed) {
                auto toggle=new QToolButton;
                toggle->setObjectName("module-toggle-"+q(section.id)); toggle->setText(q(section.title));
                toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); toggle->setArrowType(Qt::RightArrow);
                toggle->setCheckable(true); section_layout->addWidget(toggle);
                auto body=new QWidget; section_layout->addWidget(body); body->hide();
                connect(toggle,&QToolButton::toggled,body,[body,toggle](bool open) {
                    body->setVisible(open); toggle->setArrowType(open?Qt::DownArrow:Qt::RightArrow);
                });
                section_layout=new QVBoxLayout(body); section_layout->setContentsMargins(0,0,0,0); section_layout->setSpacing(8);
            } else if (!compact && !section.title.empty() && section.title!=section.tab && !section.row && section.collection.empty() && !(section.settings.size()==1 && collections.contains(section.settings.front()))) section_layout->addWidget(text(q(section.title),"title"));
            if (!section.description.empty() && !(section.settings.size()==1 && collections.contains(section.settings.front()))) section_layout->addWidget(text(q(section.description),"muted"));
            if (!section.settings.empty()) {
                auto settings=new QWidget; if(!compact && !selection_only && !section.row && section.collection.empty() && (section.settings.size()!=1 || !collections.contains(section.settings.front()))) settings->setObjectName("card"); auto settings_layout=new QVBoxLayout(settings);
                settings_layout->setContentsMargins(compact || selection_only || section.row || !section.collection.empty()?0:10,compact || selection_only || section.row || !section.collection.empty()?0:8,compact || selection_only || section.row || !section.collection.empty()?0:10,compact || selection_only || section.row || !section.collection.empty()?0:8); settings_layout->setSpacing(8);
                auto form=new QGridLayout; form->setVerticalSpacing(dense?2:6); form->setHorizontalSpacing(8);
                const int first_row=section.row && !ordered && row_headings.insert(section.tab).second?1:0;
                for (unsigned column=0;column<(section.row?section.settings.size()+1:section.columns);++column) form->setColumnStretch(column,1);
                if (section.row && !ordered) {form->setColumnStretch(0,0);form->setColumnMinimumWidth(0,row_widths.at(section.tab));}
                unsigned position=0;
                if(section.row) {
                    auto title=new QHBoxLayout; title->setContentsMargins(0,0,0,0); title->setSpacing(4);
                    if(ordered) title->addWidget(ordered->handle(group));
                    title->addWidget(text(q(section.title)),1); form->addLayout(title,first_row,0);
                }
                for (const auto& id : section.settings) {
                    const auto& field=*std::find_if(entry.manifest.settings.begin(),entry.manifest.settings.end(),[&](const auto& f) { return f.id==id; });
                    QWidget* control; const auto& value=saved_values.at(field.id);
                    if (const auto* v=std::get_if<bool>(&value)) { auto c=new QCheckBox(section.row?QString{}:q(field.label)); c->setChecked(*v); control=c; }
                    else if (const auto* v=std::get_if<int>(&value)) {
                        if (field.decimals) {
                            const auto divisor=std::pow(10.,field.decimals);
                            auto c=new QDoubleSpinBox; c->setDecimals(field.decimals); c->setRange(field.minimum/divisor,field.maximum/divisor);
                            c->setSingleStep(1./divisor); c->setKeyboardTracking(false); c->setSuffix(q(field.suffix)); control=c;
                        } else {
                            auto c=new QSpinBox; c->setRange(field.minimum,field.maximum); c->setKeyboardTracking(false); c->setSuffix(q(field.suffix)); control=c;
                        }
                        set_number(control,*v,field);
                    }
                    else if (const auto* v=std::get_if<std::string>(&value)) { auto c=new QComboBox; for (const auto& choice : field.choices) c->addItem(q(choice)); c->setCurrentText(q(*v)); control=c; }
                    else if(collections.contains(field.id)) control=collections.at(field.id);
                    else if(field.inline_choices) control=new InlineSelection(field,std::get<SettingSelection>(value),dense);
                    else control=new SelectionControl(field.choices,std::get<SettingSelection>(value),field.ordered);
                    control->setObjectName(q(field.id)); control->setAccessibleName(q(field.label)); control->setToolTip(q(field.help));
                    if (!std::holds_alternative<bool>(value)) control->setMinimumWidth(section.row?70:110);
                    page->controls[field.id]=control;
                    const int column=section.row?position+1:position%section.columns;
                    if (section.row && std::holds_alternative<bool>(value)) {form->setColumnStretch(column,0);form->setColumnMinimumWidth(column,36);}
                    const int row=section.row?0:(position/section.columns)*2;
                    if (first_row) form->addWidget(text(q(field.label),"muted"),0,column);
                    if(!section.row && !std::holds_alternative<bool>(value) && !collections.contains(field.id) && !field.inline_choices)
                        form->addWidget(text(q(field.label),"muted"),row,column);
                    if(!collections.contains(field.id) && !field.inline_choices) control->setFixedHeight(dense?24:30);
                    form->addWidget(control,section.row?first_row:row+1,column,Qt::AlignVCenter); ++position;
                }
                settings_layout->addLayout(form); section_layout->addWidget(settings);
            }
            if (section.metrics) { page->metrics=new ModuleMetrics; section_layout->addWidget(page->metrics); }
            if(ordered) ordered->add(group,priority);
            else if(!section.collection.empty()) collections.at(section.collection)->addSection(section.choice,section.title,group);
            else if(!section.tool.empty()) tool_panel(section.tool,false,section.tab.empty()?nullptr:tabs.at(section.tab))->addWidget(group);
            else if(!section.tab.empty()) tabs.at(section.tab)->addWidget(group);
            else layout->addWidget(group);
        }
        auto commands=entry.commands();
        for (const auto& command:commands) if (command.visible && !command.plot) {
            auto button=new QPushButton(q(command.label));
            button->setObjectName(command.launches?"primary":"module-command");
            button->setProperty("live-command",command.access!=extensions::CommandAccess::stopped);
            if (!command.tab.empty() && tabs.contains(command.tab)) { tab_actions.at(command.tab)->addWidget(button); tab_actions.at(command.tab)->addStretch(); }
            else tool_buttons->addWidget(button);
            connect(button,&QPushButton::clicked,this,[this,id,command] { const bool success=perform([&] {
                const auto arguments=app::command_arguments(extension_pages_.at(id)->widget,command);
                if(arguments) { const auto result=invoke_module(id,command.id,false,{{},*arguments});app::command_result(extension_pages_.at(id)->widget,result); }
            });
                auto* feedback=extension_pages_.at(id)->feedback;
                feedback->setText(success?QString{}:message_->text()); feedback->setVisible(!success);
            });
        }
        for(auto& [name,body]:tabs) body->addStretch();
        std::erase_if(commands,[](const auto& command) { return !command.plot || !command.visible; });
        if (!commands.empty()) {
            auto tools_layout=tool_panel(commands.front().tool.empty()?"Tools":commands.front().tool);
            auto bar=new QHBoxLayout; tools_layout->addLayout(bar);
            for(const auto& command:commands) {
                auto button=new QPushButton(q(command.label)); bar->addWidget(button);
                connect(button,&QPushButton::clicked,this,[this,id,command] { perform([&] {
                    invoke_module(id,command.id);
                }); });
            }
            tools_layout->parentWidget()->setFixedSize(720,520);
            auto chart=new ModulePlot; page->plot=chart; tools_layout->addWidget(chart,1);
            for(const auto& kind:{QString("PNG"),QString("CSV")}) {
                auto button=new QPushButton("Export "+kind); bar->addWidget(button);
                connect(button,&QPushButton::clicked,this,[chart,kind] {
                    if(!chart->hasData()) return;
                    const auto path=QFileDialog::getSaveFileName(chart,"Export chart",{},kind+" (*."+kind.toLower()+")");
                    if(!path.isEmpty() && !(kind=="PNG"?chart->savePng(path):chart->saveCsv(path))) QMessageBox::warning(chart,"Export failed","The file could not be saved.");
                });
            }
        }
        if (tool_factory_) if (auto* tool=tool_factory_(id,page->widget)) {
            page->tool=tool;
            const auto name=tool->windowTitle().isEmpty()?std::string("Tool"):tool->windowTitle().toStdString();
            auto body=tool_panel(name,true);
            body->parentWidget()->setFixedSize(560,340);
            body->addWidget(tool,1);
        }
        for(const auto& [name,body]:tools) {
            auto close=new QPushButton("Close"); close->setProperty("live-tool",true); close->setAutoDefault(false); body->addWidget(close,0,Qt::AlignRight);
            connect(close,&QPushButton::clicked,static_cast<QDialog*>(body->parentWidget()),&QDialog::reject);
            body->parentWidget()->setFixedHeight(std::max(body->parentWidget()->height(),body->minimumSize().height()));
        }
        page->error=text({},"muted"); page->error->hide(); layout->addWidget(page->error);
        auto reset=new QPushButton("Defaults"); reset->setVisible(collections.empty());
        tool_buttons->addStretch(); tool_buttons->addWidget(reset); layout->addLayout(tool_buttons);
        connect(reset,&QPushButton::clicked,this,[this,id] {
            if (busy_()) return;
            perform([this,id] {
                configure_module(id,extensions::defaults(host_.entries().at(id).manifest));
            });
        });
        auto save=new QTimer(page->widget); save->setSingleShot(true);
        connect(save,&QTimer::timeout,this,[this,id] {
            if(busy_()) return;
            const auto& entry=host_.entries().at(id);
            auto values=extension_pages_.at(id)->read(entry.manifest);
            if(values==entry.settings) return;
            const bool success=perform([&] { host_.configure(id,std::move(values)); });
            auto* feedback=extension_pages_.at(id)->feedback;
            feedback->setText(success?QString{}:message_->text()); feedback->setVisible(!success);
        });
        const auto changed=[save,this,id] {
            extension_pages_.at(id)->update_conditions(host_.entries().at(id).manifest,busy_());
            save->start(0);
        };
        for(const auto& [key,control]:page->controls) {
            if(auto* selection=dynamic_cast<SelectionValue*>(control)) selection->changed=changed;
            else if(auto* check=qobject_cast<QCheckBox*>(control)) connect(check,&QCheckBox::toggled,save,changed);
            else if(auto* number=qobject_cast<QSpinBox*>(control)) connect(number,&QSpinBox::valueChanged,save,changed);
            else if(auto* number=qobject_cast<QDoubleSpinBox*>(control)) connect(number,&QDoubleSpinBox::valueChanged,save,changed);
            else if(auto* choice=qobject_cast<QComboBox*>(control)) connect(choice,&QComboBox::currentTextChanged,save,changed);
        }
        if (!page_tabs) layout->addStretch();
        const auto position=static_cast<int>(std::distance(extension_pages_.begin(),extension_pages_.lower_bound(id)));
        list_->insertWidget(position,page->row);
        const auto index=pages_->indexOf(manager_)+1+position;
        pages_->insertWidget(index,page->widget); navigation_->insertItem(index,q(entry.manifest.name));
        extension_pages_.emplace(id,std::move(page));
    }
    if (!selected.isEmpty()) {
        const auto found=extension_pages_.find(selected.toStdString());
        navigation_->setCurrentRow(found==extension_pages_.end()?pages_->indexOf(manager_):pages_->indexOf(found->second->widget));
    }
}
void ExtensionsUi::update(const extensions::SessionView& view) {
    host_.update(view);
    render();
}
QStringList ExtensionsUi::enabled_features() const {
    QStringList result;
    for (const auto& [id,entry] : host_.entries())
        if (entry.requested) result.push_back(q(entry.manifest.name));
    return result;
}
QString ExtensionsUi::feature_name(const std::string& id) const {
    const auto found=host_.entries().find(id);
    return found==host_.entries().end()?q(id):q(found->second.manifest.name);
}
std::vector<extensions::ModuleReport> ExtensionsUi::module_reports() const {
    std::vector<extensions::ModuleReport> result;
    for (const auto& [id,entry]:host_.entries()) if (entry.requested) result.push_back(entry.report());
    return result;
}
extensions::Settings ExtensionsUi::module_settings(const std::string& id) const {
    const auto& entry=host_.entries().at(id);
    return extension_pages_.contains(id)?extension_pages_.at(id)->read(entry.manifest):entry.settings;
}
void ExtensionsUi::configure_module(const std::string& id,const extensions::Settings& values) {
    host_.configure(id,values);
    auto& page=*extension_pages_.at(id);
    page.baseline=values;
    for (const auto& field:host_.entries().at(id).manifest.settings) {
        if (!page.controls.contains(field.id)) continue;
        auto* control=page.controls.at(field.id);const QSignalBlocker block(control);const auto& value=values.at(field.id);
        if (const auto* v=std::get_if<bool>(&value)) static_cast<QCheckBox*>(control)->setChecked(*v);
        else if (const auto* v=std::get_if<int>(&value)) set_number(control,*v,field);
        else if (const auto* v=std::get_if<std::string>(&value)) static_cast<QComboBox*>(control)->setCurrentText(q(*v));
        else dynamic_cast<SelectionValue*>(control)->setValue(std::get<SettingSelection>(value));
    }
    page.update_conditions(host_.entries().at(id).manifest,busy_());
    for (auto* rows:page.ordered_rows) rows->sort();
    render();
}
void ExtensionsUi::enable_module(const std::string& id,bool enabled) {
    if (enabled) { host_.configure(id,module_settings(id)); host_.enable(id); }
    else host_.disable(id);
    render();
}
extensions::CommandResult ExtensionsUi::invoke_module(const std::string& id,const std::string& command,bool remote,extensions::CommandInput input) {
    const auto definition=host_.commands().find(id,command);
    if (remote && (!definition.remote || !definition.target_prompt.empty())) throw std::invalid_argument("This command is local only");
    if (definition.access==extensions::CommandAccess::stopped) {
        if (busy_()) throw std::runtime_error("Stop the current session before running this command");
        host_.configure(id,module_settings(id));
    }
    if (!remote && !definition.target_prompt.empty() && !input.target) {
        if (!picker_) throw std::runtime_error("Target selection is unavailable");
        input.target=picker_(definition.target_prompt);if(!input.target)return {definition.label,"Cancelled."};
    }
    return host_.invoke_command(id,command,std::move(input),remote);
}
std::vector<std::string> ExtensionsUi::run_order() const {
    std::vector<std::string> result;
    for (int i=0;i<order_tabs_->count();++i) result.push_back(order_tabs_->tabData(i).toString().toStdString());
    return result;
}
void ExtensionsUi::set_run_order(std::vector<std::string> order) {
    if (busy_()) throw std::runtime_error("Stop the current session before changing the run order");
    const auto current=run_order();std::set<std::string> unique;
    for (const auto& id:order) if (std::find(current.begin(),current.end(),id)==current.end() || !unique.insert(id).second)
        throw std::invalid_argument("Choose each enabled routine module at most once");
    for (const auto& id:current) if (unique.insert(id).second) order.push_back(id);
    for (const auto& id:order_) if (unique.insert(id).second) order.push_back(id);
    QStringList saved;for (const auto& id:order) saved.push_back(q(id));
    QSettings settings(order_file_,app::json_settings_format());settings.setValue("modules/order",saved);settings.sync();
    if (settings.status()!=QSettings::NoError) throw std::runtime_error("Could not save the run order");
    order_=std::move(order);refresh_list();
}
void ExtensionsUi::apply_settings() {
    if (busy_()) throw std::runtime_error("Stop the current session before applying settings");
    std::map<std::string,extensions::Settings> values;
    for (const auto& [id,page] : extension_pages_) {
        const auto& manifest=host_.entries().at(id).manifest;
        auto settings=page->read(manifest);
        extensions::validate_settings(manifest,settings);
        values.emplace(id,std::move(settings));
    }
    for (auto& [id,settings] : values) host_.configure(id,std::move(settings));
}
void ExtensionsUi::render() {
    reconcile(); refresh_list();
    if(command_revision_!=host_.commands().revision()) {
        command_revision_=host_.commands().revision();if(commands_changed)commands_changed();
    }
    const bool busy=busy_(); install_->setEnabled(!busy); reload_->setEnabled(!busy);
    for (auto& [id,page] : extension_pages_) {
        const auto& entry=host_.entries().at(id);
        page->error->setVisible(!page->error->text().isEmpty());
        if (page->plot) page->plot->setPlot(entry.plot());
        if (page->metrics && page->widget->isVisible()) page->metrics->setMetrics(entry.display);
        if (page->busy!=busy) {
            for (auto& [key,control] : page->controls) control->setEnabled(!busy);
            for (auto* rows:page->ordered_rows) rows->setEnabled(!busy);
            for (auto* button:page->widget->findChildren<QPushButton*>())
                if ((!page->tool || !page->tool->isAncestorOf(button)) && (!page->content || !page->content->isAncestorOf(button))) button->setEnabled(!busy || button->property("live-tool").toBool() || button->property("live-command").toBool());
            page->update_conditions(entry.manifest,busy);
            page->busy=busy;
        }
    }
}
}
