#include "setting_widgets.hpp"
#include "extensions_ui.hpp"
#include "presets.hpp"
#include "core/module_host/collection_settings.hpp"
#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include "command_form.hpp"
#include "module_plot.hpp"
#include "module_metrics.hpp"
#include "navigation.hpp"
#include "ordered_rows.hpp"
#include "platform/memory.hpp"
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QImageReader>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QScrollBar>
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
QLabel* text(const QString& value,const char* role=nullptr) {
    auto label=new QLabel(value); label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
    if (role) label->setObjectName(role); return label;
}

}
struct ExtensionsUi::Page {
    QWidget* widget{};
    QWidget* tool{};
    QWidget* content{};
    QWidget* row{};
    QLabel* feedback{};
    QLabel* error{};
    QLabel* warning{};
    QPushButton* enabled{};
    QPushButton* open{};
    extensions::Settings baseline,persisted;
    QLabel* status{};
    ModulePlot* plot{};
    QLabel* plot_status{};
    std::vector<QPushButton*> plot_commands,plot_exports;
    std::map<std::string,QLabel*> summaries;
    ModuleMetrics* metrics{};
    bool active{};
    std::optional<bool> busy;
    std::map<std::string,QWidget*> controls;
    std::vector<OrderedRows*> ordered_rows;
    QTimer* save{};
    std::vector<QDialog*> dialogs;
    QStringList tabs;
    std::map<QString,bool> expanded;
    std::vector<QPoint> scroll_positions;
    std::map<std::string,std::string> field_choices;
    void update_conditions(const extensions::Manifest& manifest,bool busy) {
        update_setting_conditions(manifest.settings,controls,baseline,busy);
    }
    extensions::Settings read(const extensions::Manifest& manifest) const {
        extensions::Settings result=baseline;
        for (const auto& field : manifest.settings) {
            if(!controls.contains(field.id)) continue;
            result[field.id]=read_setting_value(controls.at(field.id),field);
        }
        return result;
    }

};
ExtensionsUi::ExtensionsUi(Navigation* navigation,QStackedWidget* pages,QString bundled,QString user_root,QString state_file,std::function<bool()> busy,RegisterProviders register_providers,extensions::Host::FailureHandler failure_handler,ToolFactory tool_factory,std::function<void(automation::Launch)> launch,TargetPicker picker,PageFactory page_factory)
    : QObject(pages),host_(state_file,busy,std::move(failure_handler)),navigation_(navigation),pages_(pages),roots_{std::move(bundled),user_root},user_root_(std::move(user_root)),busy_(std::move(busy)),tool_factory_(std::move(tool_factory)),page_factory_(std::move(page_factory)),launch_(std::move(launch)),picker_(std::move(picker)),order_file_(state_file+".order.json") {
    memory_cleanup_=new QTimer(this); memory_cleanup_->setSingleShot(true);
    connect(memory_cleanup_,&QTimer::timeout,this,[this] { if(!busy_()) release_unused_memory(); });
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
    auto presets=new QPushButton("Presets…"); heading->addWidget(presets);
    connect(presets,&QPushButton::clicked,this,[this,state_file] {
        // A starting point copies the current setup. Flush pending widget edits
        // before taking that snapshot, or applying it can restore older values.
        if (busy_() || !perform([this] { apply_settings(); })) return;
        show_presets(manager_->window(),QFileInfo(state_file).dir().filePath("presets"),[this] {
            apply_settings(); auto preset=host_.preset(); QJsonArray order;
            for (const auto& id:run_order()) if (host_.entries().at(id).manifest.presets) order.append(q(id));
            preset["order"]=order; return preset;
        },[this](const QJsonObject& preset) {
            if (busy_()) throw std::runtime_error("Stop the macro before applying a preset");
            std::vector<std::string> order; std::set<std::string> unique;
            if (!preset["order"].isArray()) throw std::invalid_argument("Preset run order is missing");
            for (const auto& value:preset["order"].toArray()) {
                const auto id=value.toString().toStdString(); const auto found=host_.entries().find(id);
                if (!value.isString() || found==host_.entries().end() || !found->second.manifest.presets || !unique.insert(id).second)
                    throw std::invalid_argument("Invalid preset run order");
                order.push_back(id);
            }
            host_.apply_preset(preset);
            if (preset["defaultOrder"].toBool()) order_.clear();
            for (const auto& [id,entry]:host_.entries()) if (entry.manifest.presets) {
                release_page(id,false); extension_pages_.at(id)->baseline=entry.settings;
            }
            render();
            const auto available=run_order();
            std::erase_if(order,[&](const auto& id) { return std::find(available.begin(),available.end(),id)==available.end(); });
            set_run_order(std::move(order));
        },busy_,[this] { if (!busy_()) apply_settings(); return host_.starter_presets(); });
    });
    layout->addLayout(heading);
    layout->addWidget(text("Enabled modules appear in the sidebar and run together.","muted"));
    message_=text({},"muted"); layout->addWidget(message_);
    auto order_label=text("Run order · drag to rearrange","muted"); layout->addWidget(order_label);
    order_list_=new QTabBar; order_list_->setObjectName("module-run-order");
    order_list_->setMovable(true); order_list_->setExpanding(false); order_list_->setDrawBase(false);
    order_list_->setUsesScrollButtons(true); order_list_->setElideMode(Qt::ElideNone); order_list_->setFixedHeight(34);
    order_list_->setCursor(Qt::OpenHandCursor); order_list_->setAccessibleName("Module run order");
    order_list_->setToolTip("Drag a module left or right. Earlier modules run first when several are ready; gather interrupt still applies."); layout->addWidget(order_list_);
    connect(order_list_,&QTabBar::tabMoved,this,[this] {
        if (!busy_()) perform([&] { set_run_order(run_order()); });
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
    connect(pages_,&QStackedWidget::currentChanged,this,[this] {
        show_page(); QTimer::singleShot(0,this,[this] { release_hidden_pages(); });
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
    reconcile(); refresh_list(); show_page(); return success;
}
void ExtensionsUi::scan() {
    const QSignalBlocker blocked(pages_);
    const auto selected=pages_->currentWidget()?pages_->currentWidget()->property("feature-id").toString():QString{};
    if (!selected.isEmpty()) navigation_->setCurrentRow(pages_->indexOf(manager_));
    for (auto& [id,page] : extension_pages_) {
        const auto index=pages_->indexOf(page->widget); pages_->removeWidget(page->widget); navigation_->removeItem(index);
        list_->removeWidget(page->row); delete page->row; delete page->widget;
    }
    extension_pages_.clear();
    host_.discover(roots_); reconcile(); refresh_list();
    if (extension_pages_.contains(selected.toStdString())) navigation_->setCurrentRow(pages_->indexOf(extension_pages_.at(selected.toStdString())->widget));
    QStringList errors; for (const auto& message : host_.diagnostics()) errors.append(q(message)); message_->setText(errors.join("\n"));
    show_page();
}
void ExtensionsUi::refresh_list() {
    const auto& warnings=host_.hotbar_warnings();
    if (shared_hotbar_warning) {
        QStringList messages;
        if (const auto found=warnings.find("");found!=warnings.end()) for (const auto& message:found->second) messages.push_back(q(message));
        shared_hotbar_warning(messages.join("\n"));
    }
    for (const auto& [id,page] : extension_pages_) {
        const auto& entry=host_.entries().at(id);
        QSignalBlocker blocked(page->enabled); page->enabled->setChecked(entry.requested); page->enabled->setText(entry.requested?"Disable":"Enable");
        page->enabled->setEnabled(!busy_());
        page->open->setVisible(entry.requested);
        const auto index=pages_->indexOf(page->widget);
        if (!entry.requested && pages_->currentWidget()==page->widget) navigation_->setCurrentRow(pages_->indexOf(manager_));
        navigation_->setItemVisible(index,entry.requested);
        const auto commands=entry.commands();
        page->status->setText(!entry.issue.empty()?"Setup needed":!entry.active()?"Off":entry.participation()?"Scheduled":std::any_of(commands.begin(),commands.end(),[](const auto& command) { return command.launches; })?"Manual tools":"Connected service");
        page->status->setToolTip(q(entry.issue));
        if (page->error) { page->error->setText(q(entry.issue)); page->error->setVisible(!entry.issue.empty()); }
        const auto summaries=entry.setting_summaries();
        for (const auto& [key,label]:page->summaries) {
            const auto found=summaries.find(key); label->setText(found==summaries.end()?QString{}:q(found->second)); label->setVisible(!label->text().isEmpty());
        }
        if (page->warning) {
            QStringList messages;
            if (const auto found=warnings.find(id);found!=warnings.end()) for (const auto& message:found->second) messages.push_back(q(message));
            page->warning->setText(messages.join("\n"));page->warning->setVisible(!messages.empty());
        }
    }
    order_list_->setEnabled(!busy_());
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
        bool changed=order_list_->count()!=static_cast<int>(entries.size());
        for (int i=0;!changed && i<order_list_->count();++i) changed=order_list_->tabData(i).toString()!=q(entries[i].id);
        if (changed) {
            const auto selected=order_list_->tabData(order_list_->currentIndex()).toString();
            QSignalBlocker block(order_list_);
            while (order_list_->count()) order_list_->removeTab(0);
            for (const auto& entry:entries) {
                const auto index=order_list_->addTab(q(host_.entries().at(entry.id).manifest.name));
                order_list_->setTabData(index,q(entry.id));
                if (q(entry.id)==selected) order_list_->setCurrentIndex(index);
            }
        }
        order_list_->setVisible(order_list_->count()>1);
    } catch (const std::exception&) { order_list_->hide(); }
}
void ExtensionsUi::reconcile() {
    const QSignalBlocker blocked(pages_);
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
        auto page=std::make_unique<Page>(); page->active=entry.active(); page->baseline=saved_values; page->persisted=entry.settings;
        page->row=new QWidget; page->row->setObjectName("feature-card"); page->row->setProperty("feature-id",q(id));
        auto header=new QHBoxLayout(page->row); header->setContentsMargins(8,5,8,5);
        auto description=new QHBoxLayout; description->addWidget(text(q(entry.manifest.name),"title")); description->addStretch(); header->addLayout(description,1);
        page->enabled=new QPushButton(entry.requested?"Disable":"Enable"); page->enabled->setCheckable(true); page->enabled->setObjectName("feature-enabled"); page->enabled->setChecked(entry.requested);
        page->status=text({},"muted"); description->addWidget(page->status); header->addWidget(page->enabled);
        page->open=new QPushButton("Settings"); header->addWidget(page->open);
        connect(page->open,&QPushButton::clicked,this,[this,id] { navigation_->setCurrentRow(pages_->indexOf(extension_pages_.at(id)->widget)); });
        auto scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        page->widget=scroll; scroll->setObjectName("feature-settings"); scroll->setProperty("feature-id",q(id));
        connect(page->enabled,&QPushButton::toggled,this,[this,id](bool on) {
            if (busy_()) { refresh_list(); return; }
            perform([this,id,on] { enable_module(id,on); });
        });
        const auto position=static_cast<int>(std::distance(extension_pages_.begin(),extension_pages_.lower_bound(id)));
        list_->insertWidget(position,page->row);
        const auto index=pages_->indexOf(manager_)+1+position;
        pages_->insertWidget(index,page->widget); navigation_->insertItem(index,q(entry.manifest.name));
        extension_pages_.emplace(id,std::move(page));
    }
    if (!selected.isEmpty()) {
        const auto found=extension_pages_.find(selected.toStdString());
        navigation_->setCurrentRow(found==extension_pages_.end() || !host_.entries().at(found->first).requested
            ?pages_->indexOf(manager_):pages_->indexOf(found->second->widget));
    }
}
void ExtensionsUi::show_page() {
    auto* widget=pages_->currentWidget();
    if (!widget) return;
    const auto id=widget->property("feature-id").toString().toStdString();
    host_.set_display_interest(id.empty()?std::nullopt:std::optional(id));
    const auto found=extension_pages_.find(id);
    if (found==extension_pages_.end() || !host_.entries().at(id).requested) return;
    if (!found->second->error) build_page(id);
    render_page(id);
}
void ExtensionsUi::release_hidden_pages() {
    for (const auto& [id,page]:extension_pages_) if(page->widget!=pages_->currentWidget()) release_page(id);
}
bool ExtensionsUi::release_page(const std::string& id,bool save) {
    auto& page=extension_pages_.at(id);
    if (!page->error) return false;
    auto* scroll=static_cast<QScrollArea*>(page->widget);
    auto* body=scroll->widget();
    const auto children=body->findChildren<QDialog*>();
    const auto visible=[](const auto* dialog) { return dialog->isVisible(); };
    if (save && (std::any_of(page->dialogs.begin(),page->dialogs.end(),visible)
        || std::any_of(children.begin(),children.end(),visible))) return false;
    if (!save) {
        for (auto* dialog:page->dialogs) dialog->reject();
        for (auto* dialog:children) dialog->reject();
    }
    const auto& entry=host_.entries().at(id);
    const auto values=page->read(entry.manifest);
    if (save && !busy_() && values!=entry.settings) {
        try { host_.configure(id,values); }
        catch (const std::exception& e) { message_->setText(QString::fromUtf8(e.what())); return false; }
    }
    page->baseline=values;
    for(const auto& [key,control]:page->controls)
        if(const auto* collection=dynamic_cast<CollectionEditor*>(control)) page->field_choices[key]=collection->current();
    page->tabs.clear();
    for (const auto* bar:body->findChildren<QTabBar*>()) page->tabs.push_back(bar->tabText(bar->currentIndex()));
    page->expanded.clear();
    for (const auto* toggle:body->findChildren<QToolButton*>())
        if (toggle->objectName().startsWith("module-toggle-")) page->expanded[toggle->objectName()]=toggle->isChecked();
    page->scroll_positions.clear();
    for (const auto* area:scroll->findChildren<QScrollArea*>())
        page->scroll_positions.emplace_back(area->horizontalScrollBar()->value(),area->verticalScrollBar()->value());
    page->scroll_positions.emplace_back(scroll->horizontalScrollBar()->value(),scroll->verticalScrollBar()->value());
    page->save->stop(); page->save=nullptr;
    for (auto* dialog:page->dialogs) dialog->deleteLater();
    page->dialogs.clear(); page->controls.clear(); page->ordered_rows.clear(); page->busy.reset();
    page->tool=nullptr; page->content=nullptr; page->feedback=nullptr; page->error=nullptr; page->warning=nullptr; page->plot=nullptr; page->plot_status=nullptr; page->plot_commands.clear(); page->plot_exports.clear(); page->summaries.clear(); page->metrics=nullptr;
    connect(body,&QObject::destroyed,memory_cleanup_,[this] { memory_cleanup_->start(0); });
    scroll->takeWidget()->deleteLater();
    return true;
}
void ExtensionsUi::build_page(const std::string& id) {
    auto* page=extension_pages_.at(id).get();
    auto* scroll=static_cast<QScrollArea*>(page->widget);
    const auto& entry=host_.entries().at(id);
    const auto& saved_values=page->baseline;
    auto body=new QWidget; scroll->setWidget(body);
    auto layout=new QVBoxLayout(body); layout->setContentsMargins(12,10,12,10); layout->setSpacing(8);
    auto title=new QHBoxLayout; title->addWidget(text(q(entry.manifest.name),"heading"),1);
    layout->addLayout(title);
    page->feedback=text({},"muted"); page->feedback->hide(); layout->addWidget(page->feedback);
    if(page_factory_) if(auto* content=page_factory_(id,body)) { page->content=content;layout->addWidget(content); }
    const bool dense=entry.manifest.page && entry.manifest.page->compact;
    QLabel measure; measure.setObjectName("muted"); measure.ensurePolished();
    const auto font_metrics=measure.fontMetrics();
    auto commands=entry.commands();
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
    std::map<std::string,CollectionEditor*> collections;
    for (const auto& section:sections) if(!section.collection.empty() && !collections.contains(section.collection)) {
        const auto& field=*std::find_if(entry.manifest.settings.begin(),entry.manifest.settings.end(),[&](const auto& f) { return f.id==section.collection; });
        auto editor=collection_control(field.choices,std::get<SettingSelection>(saved_values.at(field.id)),field.label);
        if(page->field_choices.contains(field.id)) editor->select(page->field_choices.at(field.id));
        page->field_choices[field.id]=editor->current();
        collections[field.id]=editor;
        editor->copy=[this,id,collection=field.id](const std::string& choice) {
            perform([&] {
                const auto json=extensions::copy_collection_settings(host_.entries().at(id).manifest,module_settings(id),collection,choice);
                QApplication::clipboard()->setText(QString::fromUtf8(json));
            });
        };
        editor->paste=[this,id,collection=field.id](const std::string& choice) {
            if (busy_()) return;
            const bool success=perform([&] {
                const auto values=extensions::paste_collection_settings(host_.entries().at(id).manifest,module_settings(id),collection,choice,
                    QApplication::clipboard()->text().toUtf8());
                configure_module(id,values);
            });
            auto* feedback=extension_pages_.at(id)->feedback;
            feedback->setText(success?QString{}:message_->text()); feedback->setVisible(!success);
        };
        editor->restore=[this,id,collection=field.id](const std::string& choice) {
            const auto& manifest=host_.entries().at(id).manifest;
            auto& controls=extension_pages_.at(id)->controls;
            for(const auto& section:manifest.page->sections) if(section.collection==collection && section.choice==choice)
                for(const auto& key:section.settings) {
                    const auto& field=*std::find_if(manifest.settings.begin(),manifest.settings.end(),[&](const auto& f) { return f.id==key; });
                    if(field.hidden) continue;
                    if(std::any_of(manifest.page->sections.begin(),manifest.page->sections.end(),[&](const auto& other) {
                        return other.collection==collection && other.choice!=choice
                            && std::find(other.settings.begin(),other.settings.end(),key)!=other.settings.end();
                    })) continue; // Reset this field without changing shared controls.
                    if(!controls.contains(key)) {extension_pages_.at(id)->baseline[key]=field.initial;continue;}
                    write_setting_value(controls.at(key),field,field.initial);
                }
        };
    }
    std::map<std::string,QVBoxLayout*> tools;
    std::set<std::pair<std::string,QBoxLayout*>> tool_links;
    auto tool_buttons=new QHBoxLayout;
    const auto tool_panel=[&](const std::string& name,bool live=false,QBoxLayout* destination=nullptr) -> QVBoxLayout* {
        QVBoxLayout* body{}; QDialog* dialog{};
        if(tools.contains(name)) { body=tools.at(name); dialog=static_cast<QDialog*>(body->parentWidget()); }
        else {
            dialog=new QDialog(page->widget->window(),Qt::Sheet); dialog->setWindowTitle(q(name)); dialog->setWindowModality(Qt::WindowModal); dialog->setSizeGripEnabled(false); dialog->setFixedSize(480,260);
            page->dialogs.push_back(dialog);
            connect(page->widget,&QObject::destroyed,dialog,&QObject::deleteLater);
            connect(dialog,&QDialog::finished,this,[this] { QTimer::singleShot(0,this,[this] { release_hidden_pages(); }); });
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
    std::map<std::string,std::vector<int>> ordered_widths;
    std::map<std::string,QHBoxLayout*> panel_rows;
    std::map<std::string,QVBoxLayout*> panels;
    const auto destination=[&](const extensions::PageSection& section) {
        auto parent=section.tab.empty()?layout:tabs.at(section.tab);
        if(section.panel.empty()) return parent;
        if(!panel_rows.contains(section.tab)) {
            auto row=new QHBoxLayout; row->setSpacing(16); parent->addLayout(row); panel_rows[section.tab]=row;
        }
        const auto key=section.tab+"/"+section.panel;
        if(!panels.contains(key)) {
            auto panel=new QWidget; panel->setObjectName("module-panel-"+q(section.panel));
            auto body=new QVBoxLayout(panel); body->setContentsMargins(0,0,0,0); body->setSpacing(5);
            panel_rows.at(section.tab)->addWidget(panel,1,Qt::AlignTop); panels[key]=body;
        }
        return panels.at(key);
    };
    std::set<std::string> row_headings;
    std::map<std::string,int> row_widths;
    for (const auto& section:sections) if (section.row && section.order_group.empty())
        row_widths[section.tab]=std::max(row_widths[section.tab],font_metrics.horizontalAdvance(q(section.title)));
    for (const auto& section : sections) {
        if(!section.collection.empty() && section.choice!=collections.at(section.collection)->current()) continue;
        if (!section.tab.empty() && layout->indexOf(page_tabs)<0) layout->addWidget(page_tabs,1);
        if (!section.tool.empty() && section.settings.empty() && section.description.empty() && !section.metrics) {
            tool_panel(section.tool,false,section.tab.empty()?nullptr:tab_actions.at(section.tab)); continue;
        }
        const bool compact=!section.settings.empty() && !section.collapsed && section.collection.empty()
            && ((section.settings.size()==1 && std::holds_alternative<bool>(saved_values.at(section.settings.front())))
                || std::any_of(entry.manifest.settings.begin(),entry.manifest.settings.end(),[&](const auto& field) {
                    return field.id==section.settings.front() && field.label==section.title && !std::holds_alternative<SettingSelection>(field.initial)
                        && (section.settings.size()==1 || std::holds_alternative<bool>(field.initial));
                }));
        const bool selection_only=section.settings.size()==1 && std::any_of(entry.manifest.settings.begin(),entry.manifest.settings.end(),[&](const auto& field) { return field.id==section.settings.front() && field.inline_choices; });
        auto group=new QWidget; group->setObjectName("module-section-"+q(section.id)); group->setAccessibleName(q(section.title));
        group->setSizePolicy(QSizePolicy::Preferred,section.tool.empty()?QSizePolicy::Maximum:QSizePolicy::Preferred);
        OrderedRows* ordered{};
        QSpinBox* priority{};
        if(!section.order_group.empty()) {
            const auto key=section.tab+"/"+section.order_group;
            if(!ordered_groups.contains(key)) {
                ordered=new OrderedRows; ordered_groups[key]=ordered; page->ordered_rows.push_back(ordered);
                auto target=destination(section);
                auto header=new QWidget; auto headings=new QGridLayout(header);
                headings->setContentsMargins(0,0,0,0); headings->setHorizontalSpacing(8);
                auto& widths=ordered_widths[key]; widths.push_back(0);
                for(const auto& row:sections) if(row.order_group==section.order_group && row.tab==section.tab)
                    widths[0]=std::max(widths[0],font_metrics.horizontalAdvance(q(row.title))+32);
                auto label=q(section.order_group); label[0]=label[0].toUpper();
                headings->addWidget(text(label,"muted"),0,0);
                for(std::size_t i=0;i<section.settings.size();++i) {
                    const auto& field=*std::find_if(entry.manifest.settings.begin(),entry.manifest.settings.end(),[&](const auto& f) { return f.id==section.settings[i]; });
                    widths.push_back(std::max(80,font_metrics.horizontalAdvance(q(field.label))));
                    headings->addWidget(text(q(field.label),"muted"),0,int(i)+1);
                }
                int width=8*(int(widths.size())-1);
                for(std::size_t i=0;i<widths.size();++i) { headings->setColumnMinimumWidth(int(i),widths[i]); width+=widths[i]; }
                header->setFixedWidth(width); ordered->setFixedWidth(width);
                target->addWidget(header); target->addWidget(ordered);
            } else ordered=ordered_groups.at(key);
            const auto& field=*std::find_if(entry.manifest.settings.begin(),entry.manifest.settings.end(),[&](const auto& f) { return f.id==section.order_setting; });
            priority=new QSpinBox(group); priority->setObjectName(q(field.id)); priority->setRange(field.minimum,field.maximum); priority->setValue(std::get<int>(saved_values.at(field.id))); priority->hide();
            page->controls[field.id]=priority;
        }
        auto section_layout=new QVBoxLayout(group); section_layout->setContentsMargins(0,0,0,0); section_layout->setSpacing(ordered?1:5);
        if (section.collapsed) {
            auto toggle=new QToolButton;
            toggle->setObjectName("module-toggle-"+q(section.id)); toggle->setText(q(section.title));
            toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); toggle->setArrowType(Qt::RightArrow);
            toggle->setCheckable(true); section_layout->addWidget(toggle);
            auto body=new QWidget; section_layout->addWidget(body); body->hide();
            connect(toggle,&QToolButton::toggled,body,[body,toggle](bool open) {
                body->setVisible(open); toggle->setArrowType(open?Qt::DownArrow:Qt::RightArrow);
            });
            section_layout=new QVBoxLayout(body); section_layout->setContentsMargins(0,0,0,0); section_layout->setSpacing(8);
        } else if (!compact && !std::any_of(commands.begin(),commands.end(),[&](const auto& command) {return command.plot && command.tool==section.tool;}) && !section.title.empty() && section.title!=section.tab && !section.row && !(section.settings.size()==1 && collections.contains(section.settings.front()))) section_layout->addWidget(text(q(section.title),"section-heading"));
        if (!section.description.empty() && !(section.settings.size()==1 && collections.contains(section.settings.front()))) section_layout->addWidget(text(q(section.description),"muted"));
        if (!section.settings.empty()) {
            auto settings=new QWidget; auto settings_layout=new QVBoxLayout(settings);
            settings_layout->setContentsMargins(0,0,0,0); settings_layout->setSpacing(6);
            auto form=new QGridLayout; form->setVerticalSpacing(dense?4:6); form->setHorizontalSpacing(8);
            const bool inline_labels=section.inline_labels && !section.row && !selection_only;
            const auto columns=selection_only?1:section.columns;
            const bool packed=inline_labels && section.collection.empty()
                && std::none_of(section.settings.begin(),section.settings.end(),[&](const auto& key) { return collections.contains(key); });
            if(packed) form->setHorizontalSpacing(24);
            std::vector<int> label_widths(columns);
            for(std::size_t i=0;i<section.settings.size();++i) {
                const auto& field=*std::find_if(entry.manifest.settings.begin(),entry.manifest.settings.end(),[&](const auto& f) { return f.id==section.settings[i]; });
                if(std::holds_alternative<int>(field.initial) || std::holds_alternative<std::string>(field.initial))
                    label_widths[i%columns]=std::max(label_widths[i%columns],font_metrics.horizontalAdvance(q(field.label)));
            }
            const int first_row=section.row && !ordered && row_headings.insert(section.tab).second?1:0;
            if (ordered) {
                const auto& widths=ordered_widths.at(section.tab+"/"+section.order_group);
                for(std::size_t i=0;i<widths.size();++i) form->setColumnMinimumWidth(int(i),widths[i]);
            } else if(packed) form->setColumnStretch(columns,1);
            else for (unsigned column=0;column<(section.row?section.settings.size()+1:columns);++column)
                form->setColumnStretch(column,1);
            if (section.row && !ordered) {form->setColumnStretch(0,0);form->setColumnMinimumWidth(0,row_widths.at(section.tab));}
            unsigned position=0;
            if(section.row) {
                auto title=new QHBoxLayout; title->setContentsMargins(0,0,0,0); title->setSpacing(4);
                if(ordered) title->addWidget(ordered->handle(group));
                auto name=text(q(section.title)); title->addWidget(name,1);
                if (ordered) { name->setObjectName("order-label"); ordered->bind(name,group); }
                form->addLayout(title,first_row,0);
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
                    write_setting_value(control,field,*v);
                }
                else if (const auto* v=std::get_if<std::string>(&value)) control=choice_control(field,*v,section.row);
                else if(collections.contains(field.id)) control=collections.at(field.id);
                else if(field.inline_choices) control=inline_selection(field,std::get<SettingSelection>(value),dense,selection_only && section.columns>1?section.columns:0);
                else control=selection_control(field.choices,std::get<SettingSelection>(value),field.ordered);
                control->setObjectName(q(field.id)); control->setAccessibleName(q(field.label)); control->setToolTip(q(field.help));
                const bool scalar=!std::holds_alternative<bool>(value) && !std::holds_alternative<SettingSelection>(value);
                if(scalar) {
                    control->ensurePolished();
                    control->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Fixed);
                    control->setFixedWidth(std::max(64,control->sizeHint().width()));
                }
                page->controls[field.id]=control;
                const int column=section.row?position+1:position%columns;
                if (section.row && std::holds_alternative<bool>(value)) {form->setColumnStretch(column,0);form->setColumnMinimumWidth(column,36);}
                const int row=section.row?0:(position/columns)*(inline_labels?1:2);
                if (first_row) form->addWidget(text(q(field.label),"muted"),0,column);
                const bool labeled=!section.row && !std::holds_alternative<bool>(value) && !collections.contains(field.id) && !field.inline_choices;
                if(labeled && !inline_labels) {
                    auto label=text(q(field.label),"muted"); label->setBuddy(control);
                    form->addWidget(label,row,column,Qt::AlignVCenter);
                }
                if(!collections.contains(field.id) && !field.inline_choices) control->setFixedHeight(26);
                if(inline_labels) {
                    auto cell=new QHBoxLayout; cell->setContentsMargins(0,0,0,0); cell->setSpacing(7);
                    if(labeled) {
                        auto label=text(q(field.label),"muted"); label->setBuddy(control); label->setWordWrap(false); label->setFixedWidth(label_widths[column]);
                        cell->addWidget(label);
                    }
                    cell->addWidget(control); if(!field.inline_choices && !collections.contains(field.id)) cell->addStretch();
                    const int span=packed && !column && position+1==section.settings.size()?int(columns):1;
                    form->addLayout(cell,row,column,1,span);
                } else form->addWidget(control,section.row?first_row:row+1,column,Qt::AlignVCenter);
                ++position;
            }
            settings_layout->addLayout(form); section_layout->addWidget(settings);
        }
        auto summary=text({},"section-summary"); summary->hide();
        if (ordered) summary->setContentsMargins(28,0,0,0);
        page->summaries[section.id]=summary; section_layout->addWidget(summary);
        if (section.metrics) { page->metrics=new ModuleMetrics; section_layout->addWidget(page->metrics); }
        if(ordered) ordered->add(group,priority);
        else if(!section.collection.empty()) collections.at(section.collection)->addSection(section.choice,group);
        else if(!section.tool.empty()) tool_panel(section.tool,false,section.tab.empty()?nullptr:tab_actions.at(section.tab))->addWidget(group);
        else destination(section)->addWidget(group);
    }
    std::map<std::string,int> command_positions;
    for (const auto& command:commands) if (command.visible && !command.plot) {
        auto button=new QPushButton(q(command.label));
        button->setObjectName(command.launches?"primary":"module-command");
        button->setProperty("live-command",command.access!=extensions::CommandAccess::stopped);
        if (!command.tab.empty() && tabs.contains(command.tab)) tab_actions.at(command.tab)->insertWidget(command_positions[command.tab]++,button);
        else tool_buttons->addWidget(button);
        connect(button,&QPushButton::clicked,this,[this,id,command] { const bool success=perform([&] {
            const auto arguments=app::command_arguments(extension_pages_.at(id)->widget,command);
            if(arguments) { const auto result=invoke_module(id,command.id,false,{{},*arguments});app::command_result(extension_pages_.at(id)->widget,result); }
        });
            auto* feedback=extension_pages_.at(id)->feedback;
            feedback->setText(success?QString{}:message_->text()); feedback->setVisible(!success);
        });
    }
    for(auto& [name,body]:tabs) { body->addStretch(); if(tab_actions.at(name)->count()) tab_actions.at(name)->addStretch(); }
    std::erase_if(commands,[](const auto& command) { return !command.plot || !command.visible; });
    if (!commands.empty()) {
        auto tools_layout=tool_panel(commands.front().tool.empty()?"Tools":commands.front().tool);
        auto bar=new QHBoxLayout; tools_layout->addLayout(bar);
        for(const auto& command:commands) {
            auto button=new QPushButton(q(command.label)); bar->addWidget(button); page->plot_commands.push_back(button);
            connect(button,&QPushButton::clicked,this,[this,id,command] { perform([&] {
                invoke_module(id,command.id);
            }); });
        }
        tools_layout->parentWidget()->setFixedSize(760,650);
        page->plot_status=text("Choose a duration, then run the simulation.","muted"); tools_layout->addWidget(page->plot_status);
        auto chart=new ModulePlot; page->plot=chart; tools_layout->addWidget(chart,1);
        for(const auto& kind:{QString("PNG"),QString("CSV")}) {
            auto button=new QPushButton("Export "+kind); bar->addWidget(button); page->plot_exports.push_back(button); button->setEnabled(false);
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
        auto dialog=static_cast<QDialog*>(body->parentWidget());
        if ((!page->plot || !dialog->isAncestorOf(page->plot)) && (!page->tool || !dialog->isAncestorOf(page->tool))) {
            auto contents=new QWidget; auto content_layout=new QVBoxLayout(contents);
            content_layout->setContentsMargins(0,0,0,0); content_layout->setSpacing(body->spacing());
            content_layout->setSizeConstraint(QLayout::SetMinimumSize);
            while(auto item=body->takeAt(0)) {
                if(auto widget=item->widget()) {content_layout->addWidget(widget,0,item->alignment());delete item;}
                else if(auto child=item->layout()) {child->setParent(nullptr);content_layout->addLayout(child);}
                else content_layout->addItem(item);
            }
            content_layout->addStretch();
            auto scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidget(contents);
            body->addWidget(scroll,1);
            const auto hint=contents->sizeHint().expandedTo(contents->minimumSizeHint());
            dialog->setFixedSize(std::clamp(hint.width()+48,480,760),std::clamp(hint.height()+80,260,600));
        }
        auto close=new QPushButton("Close"); close->setProperty("live-tool",true); close->setAutoDefault(false); body->addWidget(close,0,Qt::AlignRight);
        connect(close,&QPushButton::clicked,static_cast<QDialog*>(body->parentWidget()),&QDialog::reject);
        body->parentWidget()->setFixedHeight(std::max(body->parentWidget()->height(),body->minimumSize().height()));
    }
    page->warning=text({},"muted"); page->warning->setWordWrap(true); page->warning->hide(); layout->addWidget(page->warning);
    page->error=text({},"muted"); page->error->hide(); layout->addWidget(page->error);
    auto reset=new QPushButton("Defaults",body); reset->setVisible(collections.empty());
    title->addWidget(reset);
    if(tool_buttons->count()) { tool_buttons->addStretch(); layout->addLayout(tool_buttons); }
    else delete tool_buttons;
    connect(reset,&QPushButton::clicked,this,[this,id] {
        if (busy_()) return;
        perform([this,id] {
            configure_module(id,extensions::defaults(host_.entries().at(id).manifest));
        });
    });
    auto save=new QTimer(body); save->setSingleShot(true); page->save=save;
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
        else if(auto* choice=dynamic_cast<ChoiceControl*>(control)) choice->changed=changed;
    }
    if (!page_tabs) layout->addStretch();
    page->error->setText(q(entry.issue));
    refresh_list();
    int index=0;
    for (auto* bar:body->findChildren<QTabBar*>()) {
        if(index<page->tabs.size()) for(int i=0;i<bar->count();++i)
            if(bar->tabText(i)==page->tabs[index]) {bar->setCurrentIndex(i);break;}
        ++index;
    }
    for(auto* toggle:body->findChildren<QToolButton*>())
        if(page->expanded.contains(toggle->objectName())) toggle->setChecked(page->expanded.at(toggle->objectName()));
    for(const auto& [key,editor]:collections) editor->selected=[this,id,key,editor,body,scroll] {
        QTimer::singleShot(0,body,[this,id,key,editor,body,scroll] {
            const auto found=extension_pages_.find(id);
            if(found==extension_pages_.end() || found->second->widget!=scroll || scroll->widget()!=body) return;
            if(found->second->field_choices.at(key)==editor->current()) return;
            if(release_page(id)) {build_page(id);render_page(id);}
        });
    };
    QTimer::singleShot(0,body,[this,id,scroll,body] {
        const auto found=extension_pages_.find(id);
        if(found==extension_pages_.end() || found->second->widget!=scroll || scroll->widget()!=body) return;
        const auto& positions=found->second->scroll_positions;
        auto areas=scroll->findChildren<QScrollArea*>(); areas.push_back(scroll);
        for(qsizetype i=0;i<areas.size() && i<qsizetype(positions.size());++i) {
            areas[i]->horizontalScrollBar()->setValue(positions[i].x());
            areas[i]->verticalScrollBar()->setValue(positions[i].y());
        }
    });
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
        write_setting_value(control,field,value);
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
    auto definition=host_.commands().find(id,command);
    if (remote && (!definition.remote || !definition.target_prompt.empty())) throw std::invalid_argument("This command is local only");
    if (definition.access==extensions::CommandAccess::stopped) {
        if (busy_()) throw std::runtime_error("Stop the current session before running this command");
        host_.configure(id,module_settings(id));
        definition=host_.commands().find(id,command);
    }
    if (!remote && !definition.target_prompt.empty() && !input.target) {
        if (!picker_) throw std::runtime_error("Target selection is unavailable");
        input.target=picker_(definition.target_prompt);if(!input.target)return {definition.label,"Cancelled."};
    }
    return host_.invoke_command(id,command,std::move(input),remote);
}
std::vector<std::string> ExtensionsUi::run_order() const {
    std::vector<std::string> result;
    for (int i=0;i<order_list_->count();++i) result.push_back(order_list_->tabData(i).toString().toStdString());
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
    for (auto& [id,settings] : values) {
        const auto& entry=host_.entries().at(id);
        if (!entry.config_valid || entry.settings!=settings) host_.configure(id,std::move(settings));
    }
}
void ExtensionsUi::render() {
    const bool busy=busy_();
    if (rendered_revision_!=host_.configuration_revision() || rendered_busy_!=busy) {
        reconcile(); refresh_list();
        rendered_revision_=host_.configuration_revision(); rendered_busy_=busy;
        install_->setEnabled(!busy); reload_->setEnabled(!busy);
    }
    if(command_revision_!=host_.commands().revision()) {
        command_revision_=host_.commands().revision();if(commands_changed)commands_changed();
    }
    show_page();
    for (const auto& [id,page]:extension_pages_) if (page->error && page->widget!=pages_->currentWidget()) render_page(id);
}
void ExtensionsUi::render_page(const std::string& id) {
    auto& page=extension_pages_.at(id);
    const auto& entry=host_.entries().at(id);
    const bool busy=busy_();
    if (page->persisted!=entry.settings) {
        for (const auto& field:entry.manifest.settings) {
            const auto& value=entry.settings.at(field.id);
            if (const auto old=page->persisted.find(field.id);old!=page->persisted.end() && old->second==value) continue;
            page->baseline[field.id]=value;
            if (const auto control=page->controls.find(field.id);control!=page->controls.end()) {
                const QSignalBlocker block(control->second);
                write_setting_value(control->second,field,value);
            }
        }
        page->persisted=entry.settings;
    }
    page->error->setVisible(!page->error->text().isEmpty());
    if (page->plot) page->plot->setPlot(entry.plot());
    if (page->metrics && page->widget->isVisible()) page->metrics->setMetrics(entry.display);
    if (page->busy!=busy) {
        for (auto& [key,control] : page->controls) control->setEnabled(!busy);
        for (auto* rows:page->ordered_rows) rows->setEnabled(!busy);
        for (auto* button:page->widget->findChildren<QPushButton*>()) {
            const bool owned=std::any_of(page->controls.begin(),page->controls.end(),[button](const auto& item) { return item.second==button || item.second->isAncestorOf(button); });
            if (!owned && (!page->tool || !page->tool->isAncestorOf(button)) && (!page->content || !page->content->isAncestorOf(button)))
                button->setEnabled(!busy || button->property("live-tool").toBool() || button->property("live-command").toBool());
        }
        page->update_conditions(entry.manifest,busy);
        page->busy=busy;
    }
    if (page->plot) {
        const auto state=entry.plot_progress();
        page->plot_status->setText(state.message.empty()?"Choose a duration, then run the simulation.":q(state.message));
        page->plot_status->setVisible(!page->plot->hasData() || state.running || (!state.message.empty() && state.message!="Ready"));
        for (auto* button:page->plot_commands) button->setEnabled(!busy && !state.running);
        for (auto* button:page->plot_exports) button->setEnabled(!state.running && page->plot->hasData());
    }
}
}
