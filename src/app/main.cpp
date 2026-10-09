#include <QIcon>
#include <QDialog>
#include "game/actions/field/movement_setup.hpp"
#include "game/detection/movement_buffs.hpp"
#include "game/detection/buffs.hpp"
#include "builtins.hpp"
#include "native_modules.hpp"
#include "target_picker.hpp"
#include "game/detection/memory_match.hpp"
#include "game/detection/daylight.hpp"
#include "game/detection/item_menu.hpp"
#include "modules/gathering/scripts.hpp"
#include "modules/shared/visit_cooldown_store.hpp"
#include "modules/planters/store.hpp"
#include "modules/planters/observations.hpp"
#include "game/detection/inventory.hpp"
#include "modules/quests/detection.hpp"
#include "core/session/session.hpp"
#include "game/actions/travel/localization.hpp"
#include "game/actions/hive/hive_setup.hpp"
#include "game/actions/travel/field_routes.hpp"
#include "game/detection/hive_view.hpp"
#include "game/detection/sprinkler_reference.hpp"
#include "game/detection/menus.hpp"
#include "game/detection/nectar.hpp"
#include "core/module_host/activity_policy.hpp"
#include "player_speed.hpp"
#include "local_data.hpp"
#include "modules/planters/ui/timers.hpp"
#include "equipment_settings.hpp"
#include "status_page.hpp"
#include "remote_settings.hpp"
#include "modules/accounts/page.hpp"
#include "remote_modules.hpp"
#include "report_images.hpp"
#include <QTabWidget>
#include "extensions_ui.hpp"
#include "navigation.hpp"
#include "theme.hpp"
#include <QApplication>
#include <QMainWindow>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QAbstractItemView>
#include <QStackedWidget>
#include <QMessageBox>
#include <QTimer>
#include <QSettings>
#include <QSignalBlocker>
#include <QLineEdit>
#include <QScrollArea>
#include <QDateTime>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <utility>
#include <set>

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
struct BuffStyle { QImage icon; QColor color; QString chart_group; };
BuffStyle buff_style(QString name) {
    static const QMap<QString,BuffStyle> styles=[] {
        QMap<QString,BuffStyle> result{
            {"blue-boost",{{},"#73aaf2","pollen-boost"}}, {"red-boost",{{},"#ee7773","pollen-boost"}},
            {"white-boost",{{},"#e9e9e9","pollen-boost"}}, {"pop-star",{{},"#94d9ef"}},
            {"melody",{{},"#e8e8e8"}}, {"jelly-bean-sharing",{{},"#f9ccff"}},
            {"guiding-star",{{},"#ead582"}}, {"reindeer-fetch",{{},"#cc4848"}},
            {"festive-mark",{{},"#7ab55e"}},
            {"focus",{{},"#6cde83"}}, {"bomb-combo",{{},"#e1e5ea"}},
            {"balloon-aura",{{},"#f0d66c"}}, {"haste",{{},"#bdc8d3"}},
            {"precision",{{},"#ba83e1"}}, {"inspire",{{},"#f3e3a1"}},
            {"honey-mark",{{},"#edbb55"}}, {"pollen-mark",{{},"#e7d86f"}},
            {"rage",{{},"#efa451"}}, {"baby-love",{{},"#eea6c2"}},
            {"haste+",{{},"#dfd26c"}}, {"oil",{{},"#e7c47e"}},
            {"smoothie",{{},"#cba1e8"}}, {"bear-morph",{{},"#cfaa83"}},
            {"comforting",{{},"#7e9eb3"}}, {"refreshing",{{},"#78b375"}},
            {"satisfying",{{},"#b398a7"}}, {"motivating",{{},"#937db3"}},
            {"invigorating",{{},"#b35951"}}
        };
        const QDir directory(":/buffs");
        for (const auto& file:directory.entryList({"*.png"},QDir::Files))
            result[file.chopped(4)].icon=QImage(directory.filePath(file)).scaled(36,36,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        return result;
    }();
    return styles.value(name.toLower().replace(' ','-'));
}
hive::reporting::Metric report_metric(const hive::extensions::Metric& metric) {
    return {q(metric.label),q(metric.value),metric.progress,buff_style(q(metric.color_key)).color};
}
QString state_name(hive::RunState state) {
    switch(state) {
    case hive::RunState::idle: return "Ready to configure";
    case hive::RunState::running: return "Running";
    case hive::RunState::paused: return "Paused";
    case hive::RunState::completed: return "Completed";
    case hive::RunState::stopped: return "Stopped";
    case hive::RunState::faulted: return "Needs attention";
    }
    return {};
}
QLabel* label(const QString& text, const char* role=nullptr) {
    auto result=new QLabel(text); result->setWordWrap(true);
    if (role) result->setObjectName(role);
    return result;
}
QWidget* card(QVBoxLayout* parent) {
    auto widget=new QWidget; widget->setObjectName("card"); parent->addWidget(widget); return widget;
}
class Window final : public QMainWindow {
    std::unique_ptr<hive::Desktop> desktop_=hive::make_desktop();
    hive::Session session_{*desktop_};
    hive::app::NativeModules native_modules_;
    std::shared_ptr<hive::automation::LandmarkNavigation> localization_;
    std::shared_ptr<hive::automation::HiveSetupResult> hive_setup_=std::make_shared<hive::automation::HiveSetupResult>();
    QSettings settings_;
    std::shared_ptr<hive::game::HiveConversion> conversion_=std::make_shared<hive::game::HiveConversion>();
    bool permissions_ready_{};
    std::shared_ptr<const hive::game::MovementSpeedSource> movement_speed_;
    std::uint64_t window_id_{};
    QLabel *permission_{}, *state_{}, *action_{};
    QPushButton* pause_{};
    std::unique_ptr<hive::reporting::Discord> discord_;
    std::unique_ptr<hive::reporting::DiscordRemote> remote_;
    std::shared_ptr<const hive::game::DetectionSource> reporting_nectar_,reporting_buffs_,reporting_upkeep_;
    hive::app::StatusPage* status_page_{};
    QTimer start_timer_;
    std::optional<hive::ClientRejoin> initial_join_;
    int countdown_{};
    unsigned focus_attempts_{};
    bool resuming_{}, resume_requested_{};
    std::optional<hive::automation::Launch> pending_feature_;
    std::unique_ptr<hive::extensions::RoutineRun> routine_;
    QString last_status_,last_outcome_,last_failure_;
    std::set<QString> seen_observation_errors_;
    QString launch_error_;
    std::unique_ptr<hive::ExtensionsUi> extensions_;
    void log(const QString& message,bool alert=false) {
        status_page_->log(message);
        if (alert) discord_->error(message);
    }
    std::uint64_t selected() const { return window_id_; }
    void follow_window(const hive::SessionSnapshot& snapshot) {
        if (snapshot.status.state==hive::RunState::running && snapshot.window.id) window_id_=snapshot.window.id;
    }
    void refresh() {
        const auto available=desktop_->windows();
        const auto previous=std::find_if(available.begin(),available.end(),[this](const auto& w) { return w.id==window_id_; });
        const hive::Window* best=previous==available.end()?nullptr:&*previous;
        for (const auto& window:available) {
            if (previous!=available.end() && window.process!=previous->process) continue;
            if (!best || window.bounds.width*window.bounds.height>best->bounds.width*best->bounds.height) best=&window;
        }
        const auto id=best?best->id:0;
        window_id_=id;
        auto p=desktop_->permissions();
        permissions_ready_=p.capture && p.input;
        permission_->setText(QString("Screen capture: %1   ·   Input: %2\n%3").arg(p.capture?"Allowed":"Permission needed",p.input?"Allowed":"Permission needed",q(p.detail)));
    }
    void prepare_start() {
        refresh();
        const auto permissions=desktop_->permissions();
        if (!permissions.input || !permissions.capture) throw std::runtime_error(permissions.detail);
        initial_join_.reset(); focus_attempts_=0;
        QTimer::singleShot(100,this,[this] {
            if (!start_timer_.isActive()) return;
            refresh();
            if (selected()) desktop_->focus_client(desktop_->inspect(selected()).window);
            else for (const auto pid:desktop_->client_processes())
                if (desktop_->focus_client_process(pid)) break;
        });
    }
    QString start_run() {
        if (start_timer_.isActive()) return "Already starting.";
        if (session_.status().state==hive::RunState::running) return "Already running.";
        if (session_.status().state==hive::RunState::paused) return "Paused. Use Resume to continue, or Stop before starting over.";
        if (!native_modules_.startup) throw std::runtime_error("Set your base move speed in Settings before starting");
        auto routine=extensions_->routine(session_,std::make_unique<hive::automation::FairActivityPolicy>(),
            hive::automation::Launch{"session-setup",{hive::automation::Permission::movement,hive::automation::Permission::interact},native_modules_.startup()});
        prepare_start(); routine_=std::move(routine); pending_feature_.reset(); launch_error_.clear(); resuming_=false;
        countdown_=3; start_timer_.start(1000); log("Starting routine in 3 seconds."); return "Starting in 3 seconds.";
    }
    QString stop_run() {
        if (remote_) remote_->cancel_commands();
        start_timer_.stop(); initial_join_.reset(); routine_.reset(); pending_feature_.reset(); launch_error_.clear(); session_.stop(); return "Stopped.";
    }
    QString pause_run() {
        if (start_timer_.isActive()) return "Still starting. Use Stop to cancel.";
        if (session_.status().state==hive::RunState::paused) return "Already paused.";
        if (session_.status().state!=hive::RunState::running) return "The macro is not running.";
        if (routine_) { routine_->request_pause(); return "Will pause after the current activity. Stop ends it immediately."; }
        session_.pause(); return "Paused.";
    }
    QString resume_run() {
        if (start_timer_.isActive()) return "Already starting.";
        if (routine_ && routine_->state()==hive::extensions::RoutineState::pausing) { routine_->cancel_pause(); return "Pending pause cancelled. Continuing."; }
        if (session_.status().state!=hive::RunState::paused) return "The macro is not paused.";
        prepare_start(); resuming_=true; countdown_=3; start_timer_.start(1000); log("Resuming in 3 seconds."); return "Resuming in 3 seconds.";
    }
    QList<hive::reporting::RemoteCommand> remote_commands() {
        using namespace hive::reporting;
        QList<RemoteCommand> commands{
            {"status","Show session, honey, backpack and nectar",false,[this](const RemoteArguments&) {
                auto reply=hive::reporting::status_reply(discord_->preview_report());
                if (start_timer_.isActive()) reply.description=resuming_?"Resuming…":"Starting…";
                return reply;
            }},
            {"nectar","Show all five nectars and their maintained floors",false,[this](const RemoteArguments&) {
                auto report=discord_->preview_report();std::optional<qint64> age;
                const auto snapshot=session_.snapshot();const hive::ObservationContext context{snapshot.window,snapshot.generation,hive::monotonic_now()};
                for(auto& gauge:report.snapshot.gauges)gauge.percent.reset();
                if(reporting_nectar_)if(const auto packet=reporting_nectar_->peek())
                    if(const auto* reading=dynamic_cast<const hive::game::NectarDetection*>(packet->value.get());reading && reading->levels.has_value(context,hive::Time(5000))) {
                        age=(context.now-reading->levels.captured).count();
                        for(int i=0;i<5 && i<report.snapshot.gauges.size();++i)report.snapshot.gauges[i].percent=reading->levels.value.percent[i];
                    }
                return nectar_reply(report,age);
            }},
            {"screenshot","Show Roblox or its honey and backpack counters",false,[this](const RemoteArguments& args) {
                RemoteReply reply{"Roblox"};
                auto frame=session_.latest_frame();
                const auto now=hive::monotonic_now();
                if (!frame || !frame->valid() || now<frame->captured || now-frame->captured>hive::Time(3000)) {
                    const auto windows=desktop_->windows();
                    auto found=std::find_if(windows.begin(),windows.end(),[this](const auto& window) { return window.id==selected(); });
                    if (found==windows.end()) found=std::max_element(windows.begin(),windows.end(),[](const auto& a,const auto& b) {
                        return a.bounds.width*a.bounds.height<b.bounds.width*b.bounds.height;
                    });
                    if (found!=windows.end()) frame=std::make_shared<hive::Frame>(desktop_->capture(found->id));
                    else frame={};
                }
                reply.image=args["view"].toString()=="counters"?hive::app::resource_image(std::move(frame)):hive::app::game_image(std::move(frame));
                if (reply.image.isNull()) reply.description="No Roblox screenshot available. Check that Roblox is open and screen capture is allowed.";
                return reply;
            },{{"view","Screenshot area",RemoteOptionType::text,false,{"game","counters"},1,32}}},
            {"log","Show recent macro activity",false,[this](const RemoteArguments& args) {
                const auto recent=status_page_->recent_log(args["lines"].toInt(20));
                return RemoteReply{"Recent activity",recent.isEmpty()?QString("No activity yet."):recent};
            },{{"lines","Maximum recent lines to include",RemoteOptionType::integer,false,{},1,100}}},
            {"rejoin","Rejoin the configured server and restart the farming routine",true,[this](const RemoteArguments&) {
                if (start_timer_.isActive()) return RemoteReply{"Rejoin","Already starting or rejoining."};
                if (!native_modules_.startup) throw std::runtime_error("Set your base move speed in Settings first");
                const auto permissions=desktop_->permissions();
                if (!permissions.capture || !permissions.input) throw std::runtime_error(permissions.detail);
                refresh();const auto old=selected()?desktop_->inspect(selected()).window:hive::Window{};
                stop_run();auto rejoin=session_.connect_client(hive::monotonic_now(),old);
                start_run();initial_join_=std::move(rejoin);
                return RemoteReply{"Rejoin","Rejoining the configured server, then restarting the routine."};
            }},
            {"start","Start the farming routine",true,[this](const RemoteArguments&) { return RemoteReply{"Start",start_run()}; }},
            {"pause","Pause after the current activity",true,[this](const RemoteArguments&) { return RemoteReply{"Pause",pause_run()}; }},
            {"resume","Resume the macro",true,[this](const RemoteArguments&) { return RemoteReply{"Resume",resume_run()}; }},
            {"stop","Stop the macro immediately",true,[this](const RemoteArguments&) { return RemoteReply{"Stop",stop_run()}; }}
        };
        for(auto command:hive::app::module_remote_commands(*extensions_)) {
            if(command.name=="help" || std::any_of(commands.begin(),commands.end(),[&](const auto& existing){return existing.name==command.name;})) {
                log("Module command name conflicts with an application command: "+command.name);continue;
            }
            commands.append(std::move(command));
        }
        return commands;
    }

public:
    explicit Window(const QString& data):settings_(QDir(data).filePath("settings.json"),hive::app::json_settings_format()) {
        setWindowTitle(QGuiApplication::applicationDisplayName()); setFixedSize(760,580);
        setWindowFlag(Qt::WindowMaximizeButtonHint,false); setWindowFlag(Qt::WindowFullscreenButtonHint,false);
        auto root=new QWidget; auto shell=new QHBoxLayout(root); shell->setContentsMargins(0,0,0,0); shell->setSpacing(0); setCentralWidget(root);
        auto sidebar=new QWidget; sidebar->setObjectName("sidebar"); sidebar->setFixedWidth(132); auto side=new QVBoxLayout(sidebar); side->setContentsMargins(9,12,9,10);
        side->addWidget(label(QGuiApplication::applicationDisplayName(),"brand"));  side->addSpacing(10);
        auto navigation=new hive::Navigation; navigation->addItems({"Settings"}); side->addWidget(navigation,1);  shell->addWidget(sidebar);
        auto workspace=new QWidget; auto workspace_layout=new QVBoxLayout(workspace); workspace_layout->setContentsMargins(0,0,0,0); workspace_layout->setSpacing(0);
        auto pages=new QStackedWidget; workspace_layout->addWidget(pages,1); shell->addWidget(workspace,1);
        auto session_bar=new QWidget; session_bar->setObjectName("session-bar"); auto bar=new QHBoxLayout(session_bar); bar->setContentsMargins(12,8,12,8); bar->setSpacing(6);
        state_=label("Ready","title"); bar->addWidget(state_,1);
        action_=label("Enable your features, then start the macro.","muted");
        pause_=new QPushButton("Pause"); pause_->setEnabled(false); auto stop=new QPushButton("Stop · Esc"); stop->setObjectName("stop"); stop->setEnabled(false);
        auto start_macro=new QPushButton("Start macro"); start_macro->setObjectName("primary");
        bar->addWidget(start_macro); bar->addWidget(pause_); bar->addWidget(stop); workspace_layout->addWidget(session_bar);
        auto page=[&](const QString& title,const QString& description) {
            auto scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
            auto widget=new QWidget; scroll->setWidget(widget); auto layout=new QVBoxLayout(widget); layout->setContentsMargins(12,12,12,10); layout->setSpacing(8);
            layout->addWidget(label(title,"heading")); if(!description.isEmpty()) layout->addWidget(label(description,"muted")); pages->addWidget(scroll); return layout;
        };
        auto run_routine=start_macro;
        auto player_speed=new hive::app::PlayerSpeed(settings_);
        auto setup=page("Settings",{});
        auto core_tabs=new QTabWidget; setup->addWidget(core_tabs);
        auto player_panel=new QWidget; auto player_page=new QVBoxLayout(player_panel); player_page->setContentsMargins(0,8,0,0);
        auto connection_panel=new QWidget; auto connection_page=new QVBoxLayout(connection_panel); connection_page->setContentsMargins(0,8,0,0);
        core_tabs->addTab(player_panel,"Player && equipment"); core_tabs->addTab(connection_panel,"Connection");
        native_modules_.accounts=std::make_shared<hive::features::accounts::Controller>(hive::features::accounts::Credentials{
            [this](const std::string& key) { return settings_.value("accounts/"+q(key)).toString(); },
            [this](const std::string& key,const QString& value) {
                if(value.isEmpty())settings_.remove("accounts/"+q(key));
                else settings_.setValue("accounts/"+q(key),value);
                settings_.sync();
                if(settings_.status()!=QSettings::NoError)throw std::runtime_error("Could not save Accounts settings.");
            }
        });
        native_modules_.accounts->suggested_server=[this] { return settings_.value("recovery/serverLink").toString(); };
        auto speed_card=new QVBoxLayout(card(player_page)); speed_card->setContentsMargins(10,10,10,10); speed_card->addWidget(player_speed);
        auto equipment=new hive::app::EquipmentSettings(settings_); player_page->addWidget(equipment);
        native_modules_.sprinklers=equipment->sprinklers();
        conversion_->configure(equipment->conversion()); session_.configure_conversion(conversion_);
        auto connection=new QVBoxLayout(card(connection_page)); connection->setContentsMargins(10,10,10,10);
        permission_=label({},"muted"); connection->addWidget(permission_); auto permissions_row=new QHBoxLayout; auto screen_permission=new QPushButton("Allow screen capture"); auto input_permission=new QPushButton("Allow input"); permissions_row->addWidget(screen_permission); permissions_row->addWidget(input_permission); connection->addLayout(permissions_row);
#ifdef Q_OS_MACOS
        screen_permission->setText("Screen Recording…");input_permission->setText("Accessibility…");
#endif
        screen_permission->setObjectName("screen-permission");input_permission->setObjectName("input-permission");
        auto hive_settings=new QVBoxLayout(card(player_page));
        hive_settings->addWidget(label("Hive","title"));
        auto existing_hive=new QComboBox;
        for (int slot=1;slot<=6;++slot) existing_hive->addItem(QString("Slot %1").arg(slot),slot);
        existing_hive->setObjectName("hive-slot");
        existing_hive->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        existing_hive->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Fixed);
        existing_hive->setCurrentIndex(std::clamp(settings_.value("game/existingHive",1).toInt(),1,6)-1);
        auto hive_row=new QHBoxLayout; hive_row->setSpacing(8); hive_row->addWidget(existing_hive); hive_row->addSpacing(12);
        hive_row->addWidget(new QLabel("Bees"));
        auto bee_count=new QSpinBox; bee_count->setObjectName("bee-count"); bee_count->setRange(0,50);
        bee_count->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Fixed);
        bee_count->setKeyboardTracking(false); bee_count->setValue(std::clamp(settings_.value("game/beeCount",50).toInt(),0,50));
        hive_row->addWidget(bee_count); hive_row->addSpacing(12);
        auto use_cannon=new QCheckBox("Cannon + glider"); use_cannon->setObjectName("use-cannon");
        use_cannon->setToolTip("Use cannon routes and an equipped glider for travel and returns.");
        use_cannon->setChecked(settings_.value("game/useCannon",true).toBool()); hive_row->addWidget(use_cannon); hive_row->addStretch();
        hive_settings->addLayout(hive_row);
        native_modules_.travel_access={unsigned(bee_count->value()),use_cannon->isChecked()};
        connect(existing_hive,&QComboBox::currentIndexChanged,this,[this,existing_hive] {
            hive_setup_->reset(); settings_.setValue("game/existingHive",existing_hive->currentData().toInt());
        });
        player_page->addStretch();
        auto recovery_settings=new QVBoxLayout(card(connection_page));
        recovery_settings->addWidget(label("Private server link","title"));
        auto server_link=new QLineEdit(settings_.value("recovery/serverLink").toString());
        server_link->setPlaceholderText("Leave empty to join a public server");
        server_link->setMaxLength(2048); recovery_settings->addWidget(server_link);
        auto recovery_error=label({},"muted"); recovery_error->hide(); recovery_settings->addWidget(recovery_error);
        try { session_.configure_rejoin(hive::game::JoinTarget(server_link->text().trimmed().toStdString())); }
        catch (const std::exception& error) { recovery_error->setText(q(error.what())); recovery_error->show(); }
        connect(server_link,&QLineEdit::editingFinished,this,[this,server_link,recovery_error] {
            try {
                const auto link=server_link->text().trimmed();
                session_.configure_rejoin(hive::game::JoinTarget(link.toStdString()));
                settings_.setValue("recovery/serverLink",link); server_link->setText(link); recovery_error->hide();
            } catch (const std::exception& error) {
                server_link->setText(settings_.value("recovery/serverLink").toString());
                QMessageBox::warning(this,"Rejoin server",q(error.what()));
            }
        });
        connection_page->addStretch();
        setup->addStretch();
        auto status_page=page("Status",{}); navigation->addItem("Status");
        status_page->addWidget(action_);
        auto enabled_features=label({},"muted"); status_page->addWidget(enabled_features);
        const auto report_frame=[this]() -> std::shared_ptr<const hive::Frame> {
            const auto frame=session_.latest_frame(); const auto snapshot=session_.snapshot();
            if (!frame || !frame->valid() || frame->window.id!=snapshot.window.id || frame->window.process!=snapshot.window.process
                || frame->window.bounds!=snapshot.window.bounds || frame->captured>snapshot.now || snapshot.now-frame->captured>hive::Time(3000)) return {};
            return frame;
        };
        discord_=std::make_unique<hive::reporting::Discord>([this] {
            const auto snapshot=session_.snapshot();
            hive::reporting::Snapshot report{state_->text(),snapshot.feature?q(snapshot.feature->activity_label):action_->text(),{}};
            std::map<std::string,double> thresholds;
            if (extensions_) for (const auto& module:extensions_->module_reports()) {
                hive::reporting::Section section{q(module.name),{},q(module.id),module.counters};
                for (const auto& metric:module.metrics) section.metrics.append(report_metric(metric));
                for (const auto& threshold:module.thresholds) thresholds[threshold.id]=threshold.minimum;
                report.sections.push_back(std::move(section));
            }
            report.counters={{"disconnects","Disconnects",snapshot.recovery.disconnects,snapshot.session_id},
                {"rejoins","Successful rejoins",snapshot.recovery.rejoins,snapshot.session_id}};
            const hive::ObservationContext context{snapshot.window,snapshot.generation,snapshot.now};
            const auto readings=session_.observations(); report.trend.name="Backpack";
            if (readings.state.honey.has_value(context,hive::Time(5000))) report.honey=readings.state.honey.value;
            if (readings.state.backpack_percent.has_value(context,hive::Time(5000))) report.trend.percent=readings.state.backpack_percent.value;
            for (const auto* name:{"Comforting","Refreshing","Satisfying","Motivating","Invigorating"}) {
                const auto id=std::string("nectar/")+name; const auto found=thresholds.find(id);
                report.gauges.append(hive::reporting::Gauge{name,{},q(id),found==thresholds.end()?std::nullopt:std::optional<double>(found->second)});
                report.gauges.back().color=buff_style(name).color;
            }
            if (reporting_nectar_) if (const auto result=reporting_nectar_->peek()) {
                const auto* nectar=dynamic_cast<const hive::game::NectarDetection*>(result->value.get());
                if (nectar && nectar->levels.has_value(context,hive::Time(5000)))
                    for (int i=0;i<5;++i) report.gauges[i].percent=nectar->levels.value.percent[i];
            }
            for (const auto& type:hive::game::tracked_buffs()) {
                const auto name=QString::fromUtf8(type.name.data(),qsizetype(type.name.size()));
                report.buffs.append(hive::reporting::Gauge{name,{},"buff/"+name,{},hive::reporting::Level{{},double(type.maximum)}});
            }
            if (reporting_upkeep_) {
                if (const auto packet=reporting_upkeep_->peek()) {
                    const auto* value=dynamic_cast<const hive::game::BuffDetection*>(packet->value.get());
                    const auto& stamp=packet->context;
                    const hive::Observation<bool> reading{hive::Knowledge::present,true,stamp.now,stamp.window,stamp.generation};
                    if (value && reading.has_value(context,hive::Time(10000)))
                        for (const auto& observed:value->buffs) for (auto& buff:report.buffs)
                            if (q(observed.name)==buff.name && observed.active) {
                                buff.percent=*observed.active?100:0;
                                if (buff.level) buff.level->value=observed.stacks;
                            }
                }
            }
            const auto movement_start=report.buffs.size();
            for (const auto* name:{"Haste","Haste+","Oil","Smoothie","Bear morph"})
                report.buffs.append(hive::reporting::Gauge{name,{},"buff/"+QString(name),{},hive::reporting::Level{{},QString(name)=="Haste"?10.:1.}});
            for (auto& buff:report.buffs) {
                const auto style=buff_style(buff.name); buff.icon=style.icon; buff.color=style.color; buff.chart_group=style.chart_group;
            }
            if (reporting_buffs_) if (const auto packet=reporting_buffs_->peek()) {
                const auto* value=dynamic_cast<const hive::game::MovementBuffDetection*>(packet->value.get());
                const auto& stamp=packet->context;
                const hive::Observation<bool> reading{hive::Knowledge::present,true,stamp.now,stamp.window,stamp.generation};
                if (value && value->buffs.layout_known && reading.has_value(context,hive::Time(5000))) {
                    for (auto i=movement_start;i<report.buffs.size();++i) { report.buffs[i].percent=0; report.buffs[i].level->value=0; }
                    for (const auto& buff:value->buffs.candidates) {
                        const int index=buff.name=="Haste"?0:buff.name=="HastePlus"?1:buff.name=="Oil"?2:buff.name=="Smoothie"?3:buff.name.starts_with("Bear")?4:-1;
                        if (index>=0) { report.buffs[movement_start+index].percent=100; report.buffs[movement_start+index].level->value=1; }
                        if (index==0) report.buffs[movement_start].level->value=buff.stacks;
                    }
                }
            }
            return report;
        },[report_frame] { return hive::app::game_image(report_frame()); });
        discord_->set_live_image_source([report_frame] { return hive::app::resource_image(report_frame()); });
        status_page_=new hive::app::StatusPage(settings_,*discord_,nullptr,QDir(data).filePath("activity.log")); status_page->addWidget(status_page_,1);
        navigation->on_selected=[pages](int index) { pages->setCurrentIndex(index); };
        navigation->setCurrentRow(0);
        auto bundled=QCoreApplication::applicationDirPath()+"/extensions";
#ifdef Q_OS_MACOS
        bundled=QCoreApplication::applicationDirPath()+"/../Resources/extensions";
#endif
        const auto pattern_folder=QDir(data).filePath("patterns");
        const auto read_patterns=[this,pattern_folder] {
#ifdef _WIN32
            const auto path=std::filesystem::path(pattern_folder.toStdWString());
#else
            const auto path=std::filesystem::path(pattern_folder.toStdString());
#endif
            for (const auto& error:hive::features::gathering::install_default_patterns(path)) log(q("Pattern: "+error));
            auto files=hive::features::gathering::read_pattern_files(path);
            for (const auto& error : files.errors) log(q("Pattern: "+error));
            return std::move(files.presets);
        };
        native_modules_.patterns=read_patterns();
        const auto movement_folder=QDir(data).filePath("movement");
#ifdef _WIN32
        native_modules_.movement=std::make_shared<hive::movement::Library>(std::filesystem::path(movement_folder.toStdWString()));
#else
        native_modules_.movement=std::make_shared<hive::movement::Library>(std::filesystem::path(movement_folder.toStdString()));
#endif

        auto pattern_buttons=new QHBoxLayout;
        auto open_patterns=new QPushButton("Open pattern folder");
        auto reload_patterns=new QPushButton("Reload patterns");
        auto open_movement=new QPushButton("Open movement folder");
        pattern_buttons->addWidget(open_patterns); pattern_buttons->addWidget(open_movement); pattern_buttons->addWidget(reload_patterns); pattern_buttons->addStretch();
        connect(open_movement,&QPushButton::clicked,this,[this,movement_folder] {
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(movement_folder))) log("Could not open the movement folder.");
        });
        player_page->insertLayout(player_page->count()-1,pattern_buttons);
        connect(open_patterns,&QPushButton::clicked,this,[this,pattern_folder] {
            if (!QDir().mkpath(pattern_folder) || !QDesktopServices::openUrl(QUrl::fromLocalFile(pattern_folder)))
                log("Could not open the pattern folder.");
        });
        const auto cooldowns=[&](const char* file) {
            const auto path=QDir(data).filePath(file);
#ifdef _WIN32
            const auto saved_path=std::filesystem::path(path.toStdWString());
#else
            const auto saved_path=std::filesystem::path(path.toStdString());
#endif
            return std::make_shared<hive::features::visits::Cooldowns>(hive::features::visits::cooldown_file_store(saved_path),
                [] { return std::chrono::time_point_cast<hive::Time>(std::chrono::system_clock::now()); });
        };
        native_modules_.quests.cooldowns=cooldowns("quest-cooldowns.json");
        native_modules_.collection.cooldowns=cooldowns("collection-cooldowns.json");
        const auto item_orders_file=QDir(data).filePath("collection-orders.json");
#ifdef _WIN32
        const auto item_orders_path=std::filesystem::path(item_orders_file.toStdWString());
#else
        const auto item_orders_path=std::filesystem::path(item_orders_file.toStdString());
#endif
        native_modules_.collection.item_orders=std::make_shared<hive::features::collection::ItemOrders>(hive::features::collection::item_order_file_store(item_orders_path));
        native_modules_.combat.cooldowns=cooldowns("combat-cooldowns.json");
        const auto planter_file=QDir(data).filePath("planters.json");
#ifdef _WIN32
        const auto planter_path=std::filesystem::path(planter_file.toStdWString());
#else
        const auto planter_path=std::filesystem::path(planter_file.toStdString());
#endif
        native_modules_.planters.account=std::make_shared<hive::features::planters::Account>(hive::features::planters::file_store(planter_path));
        native_modules_.planters.clock=[] { return std::chrono::time_point_cast<hive::Time>(std::chrono::system_clock::now()); };
        native_modules_.planters.context=[this] {
            const auto snapshot=session_.snapshot();
            return hive::ObservationContext{snapshot.window,snapshot.generation,hive::monotonic_now()};
        };
        native_modules_.quests.fields=hive::features::quests::default_fields();
        auto inventory_requests=std::make_shared<hive::game::InventoryRequests>();
        std::vector<hive::game::DetectorConfig> detectors{
            {"enemies",hive::game::enemy_detector()},
            {"memory-board",hive::game::memory_match_detector()},
            {"daylight",hive::game::daylight_detector(),hive::Time(2000),hive::game::DetectionMode::pixels,true},
            {"item-menu",hive::game::item_menu_detector(),hive::Time(750),hive::game::DetectionMode::text,true},
            {"buff-upkeep",hive::game::buff_detector(),hive::Time(6000),hive::game::DetectionMode::pixels},
            {"field-boosts",hive::game::field_boost_detector(),hive::Time(1000),hive::game::DetectionMode::pixels},
            {"movement-buffs",hive::game::movement_buff_detector(),hive::Time(100),hive::game::DetectionMode::pixels,true,hive::game::DetectionPriority::responsive},
            {"respawn-menu",hive::game::respawn_detector()},
            {"nectar",hive::game::nectar_detector(),hive::Time(750),hive::game::DetectionMode::pixels},
            {"planters",hive::game::planter_detector()},
            {"inventory",hive::game::inventory_detector(hive::game::inventory_layout(),inventory_requests)},
            {"blooms",hive::game::bloom_detector(),hive::Time(250),hive::game::DetectionMode::pixels},
            {"npc-conversation",hive::game::conversation_detector()},
            {"quest-cards",hive::features::quests::desktop_detector(native_modules_.quests.fields),hive::Time(750)},
            {"navigation-landmarks",hive::game::landmark_detector(hive::game::interaction_anchors()),hive::Time(750)},
            {"interaction-prompt",hive::game::interaction_prompt_detector(),hive::Time(100),hive::game::DetectionMode::pixels},
            {"sprinkler-reference",hive::game::sprinkler_reference_detector(),hive::Time(500),hive::game::DetectionMode::pixels},
            {"spawn-view",hive::game::spawn_view_detector(),hive::Time(250),hive::game::DetectionMode::pixels},
            {"hive-view",hive::game::hive_view_detector(),hive::Time(125),hive::game::DetectionMode::pixels},
            {"hive-slots",hive::game::hive_slots_detector(),hive::Time(250),hive::game::DetectionMode::pixels}};
        for (auto& detector:detectors) detector.on_demand=true;
        auto channels=session_.configure_detectors(std::move(detectors));
        reporting_upkeep_=channels.at("buff-upkeep");
        native_modules_.field_boosts=channels.at("field-boosts");
        session_.configure_hotbar(native_modules_.boosting);
        reporting_nectar_=channels.at("nectar"); reporting_buffs_=channels.at("movement-buffs");
        native_modules_.planters.inventory=hive::game::inventory_source(channels.at("inventory"),inventory_requests);
        native_modules_.planters.detection=hive::game::planter_source(channels.at("planters"));
        native_modules_.planters.observations=hive::features::planters::interaction_source(
            hive::game::nectar_source(channels.at("nectar")),native_modules_.planters.detection);
        const auto sprinkler_references=channels.at("sprinkler-reference");
        const auto spawn_view=hive::game::landmark_source(channels.at("spawn-view"),"spawn.reference");
        const auto hive_slots=hive::game::hive_slots_source(channels.at("hive-slots"));
        const auto hive_proximity=hive::game::landmark_source(channels.at("navigation-landmarks"),"hive.proximity");
        const auto hive_reference=hive::game::hive_reference_source(channels.at("hive-view"),hive_proximity);
        native_modules_.transport_prompt=hive::game::landmark_source(channels.at("interaction-prompt"),"interaction.prompt");
        localization_=std::make_shared<hive::automation::LandmarkNavigation>(channels.at("navigation-landmarks"),
            std::map<std::string,std::shared_ptr<const hive::game::LandmarkSource>>{
                {"hive.backboard",hive_reference},
                {"interaction.prompt",native_modules_.transport_prompt}});
        native_modules_.blooms=channels.at("blooms");
        native_modules_.quests.inventory=native_modules_.planters.inventory;
        native_modules_.quests.hive_view=channels.at("hive-view");
        native_modules_.quests.conversation=hive::game::conversation_source(channels.at("npc-conversation"));
        native_modules_.quests.observations=hive::features::quests::detection_source(channels.at("quest-cards"));
        native_modules_.quests.browser=hive::features::quests::browser_source(channels.at("quest-cards"));
        native_modules_.navigation.perception=hive::automation::hive_navigation(localization_,hive_setup_);
        const hive::app::HiveSources hive_sources{hive_slots,spawn_view,hive_reference,hive_proximity,hive_setup_,hive::game::respawn_source(channels.at("respawn-menu")),[existing_hive] { return existing_hive->currentData().toUInt(); },native_modules_.movement};
        const auto configure_connection=[this,hive_sources](std::optional<double> speed) {
            const auto reconnect=[hive_sources,speed](hive::automation::ReconnectStart start) {
                auto options=hive_sources.options(speed.value_or(28)); options.existing_slot=0;
                return hive::automation::reconnect_to_hive(hive_sources.slot_source,hive_sources.spawn,
                    hive_sources.reference,hive_sources.proximity,options,start);
            };
            session_.configure_connection_recovery(
                [reconnect] { return reconnect(hive::automation::ReconnectStart::request); },
                [reconnect] { return reconnect(hive::automation::ReconnectStart::loading); });
        };
        configure_connection(player_speed->base_speed());
        native_modules_.screen_reference=hive::game::sprinkler_reference_source(sprinkler_references);
        native_modules_.movement_setup=[] { return hive::automation::prepare_movement(); };
        native_modules_.collection.memory=channels.at("memory-board");
        native_modules_.collection.daylight=channels.at("daylight");
        native_modules_.collection.inventory=native_modules_.planters.inventory;
        native_modules_.collection.item_menus=channels.at("item-menu");
        const auto enemy_observations=channels.at("enemies");
        hive::app::configure_travel_modules(native_modules_,player_speed->base_speed(),hive_sources,enemy_observations);
        const auto movement_buffs=channels.at("movement-buffs");
        if (const auto base=player_speed->base_speed()) movement_speed_=hive::game::movement_speed_source(movement_buffs,*base);
        session_.configure_speed(movement_speed_);
        if (native_modules_.navigation.routes) session_.configure_navigation(native_modules_.navigation);
        auto launch_feature=[this](hive::automation::Launch launch) {
            const auto state=session_.status().state;
            if (start_timer_.isActive() || state==hive::RunState::running || state==hive::RunState::paused)
                throw std::runtime_error("Stop the current session before starting another operation");
            prepare_start();
            launch_error_.clear();
            routine_.reset(); pending_feature_=std::move(launch);
            resuming_=false; countdown_=3; start_timer_.start(1000);
            log("Starting in 3 seconds.");
        };
        extensions_=std::make_unique<hive::ExtensionsUi>(navigation,pages,bundled,data+"/extensions",data+"/extension-settings.json",[this] {
            const auto state=session_.status().state;
            return start_timer_.isActive() || state==hive::RunState::running || state==hive::RunState::paused;
        },[this](hive::extensions::Host& host) {
            session_.configure_server_requests(host.request_source());
            hive::extensions::register_builtin_modules(host);
            hive::app::register_native_modules(host,native_modules_);
        },[this](const std::string& id,const std::string& reason) {
            const auto message=extensions_->feature_name(id)+" unavailable · "+q(reason);
            log(message); discord_->notice(message);
            if (pending_feature_ && pending_feature_->module_id==id) { pending_feature_.reset(); start_timer_.stop(); }
            session_.stop_feature(id,reason);
        },[this](const std::string& id,QWidget* parent)->QWidget* {
            if (id!="hive.native-planters") return nullptr;
            return hive::features::planters::planter_timers(native_modules_.planters.account,parent);
        },launch_feature,[this](const std::string& prompt) -> std::optional<hive::WindowPoint> {
            refresh();
            if (!selected()) throw std::runtime_error("Open Roblox with the target visible first");
            if (!desktop_->permissions().capture) throw std::runtime_error("Screen capture permission is required");
            return hive::app::pick_window_point(desktop_->capture(selected()),QString::fromStdString(prompt),this);
        },[this](const std::string& id,QWidget* parent)->QWidget* {
            return id=="hive.native-accounts"?new hive::features::accounts::Page(native_modules_.accounts,parent):nullptr;
        });
        player_speed->on_changed=[this,player_speed,bee_count,use_cannon,equipment,hive_sources,enemy_observations,read_patterns,movement_buffs,configure_connection] {
            bool navigation_changed=false;
            try {
                const auto state=session_.status().state;
                if (start_timer_.isActive() || state==hive::RunState::running || state==hive::RunState::paused)
                    throw std::runtime_error("Stop the session before changing movement speed");
                extensions_->apply_settings();
                auto updated=native_modules_;
                updated.travel_access={unsigned(bee_count->value()),use_cannon->isChecked()};
                updated.sprinklers=equipment->sprinklers();
                updated.patterns=read_patterns();
                hive::app::configure_travel_modules(updated,player_speed->base_speed(),hive_sources,enemy_observations);
                session_.configure_navigation(updated.navigation.routes?updated.navigation:hive::automation::NavigationServices{});
                navigation_changed=true;
                const auto base=player_speed->base_speed();
                auto speed=base?hive::game::movement_speed_source(movement_buffs,*base):nullptr;
                session_.configure_speed(speed);
                extensions_->replace_providers([&](hive::extensions::Host& host) {
                    hive::extensions::register_builtin_modules(host);
                    hive::app::register_native_modules(host,updated);
                });
                configure_connection(base);
                conversion_->configure(equipment->conversion());
                settings_.setValue("game/beeCount",bee_count->value()); settings_.setValue("game/useCannon",use_cannon->isChecked());
                movement_speed_=std::move(speed); native_modules_=std::move(updated); routine_.reset(); hive_setup_->reset();
                return true;
            } catch (const std::exception& e) {
                if (navigation_changed) {
                    session_.configure_navigation(native_modules_.navigation.routes?native_modules_.navigation:hive::automation::NavigationServices{});
                    session_.configure_speed(movement_speed_);
                }
                launch_error_=q(e.what()); log(launch_error_,true); return false;
            }
        };
        const auto update_travel=[this,player_speed,bee_count,use_cannon] {
            if (player_speed->on_changed()) return;
            const QSignalBlocker bees_block(bee_count),cannon_block(use_cannon);
            bee_count->setValue(int(native_modules_.travel_access.bees)); use_cannon->setChecked(native_modules_.travel_access.cannon);
        };
        connect(bee_count,&QSpinBox::valueChanged,this,update_travel);
        connect(use_cannon,&QCheckBox::toggled,this,update_travel);
        equipment->on_changed=player_speed->on_changed;
        connect(reload_patterns,&QPushButton::clicked,this,[this,player_speed] {
            if (player_speed->on_changed()) log("Patterns reloaded.");
        });
        connect(run_routine,&QPushButton::clicked,this,[this] {
            try { start_run(); }
            catch (const std::exception& e) { launch_error_=q(e.what()); log(launch_error_,true); }
        });
        connect(screen_permission,&QPushButton::clicked,this,[this] { desktop_->request_capture_permission(); refresh(); });
        connect(input_permission,&QPushButton::clicked,this,[this] { desktop_->request_input_permission(); refresh(); });
        connect(stop,&QPushButton::clicked,this,[this] { stop_run(); });
        connect(pause_,&QPushButton::pressed,this,[this] { resume_requested_=pause_->text()=="Resume"; });
        connect(pause_,&QPushButton::clicked,this,[this] {
            try {
                if (resume_requested_ || (routine_ && routine_->state()==hive::extensions::RoutineState::pausing)) resume_run();
                else pause_run();
            } catch (const std::exception& e) { session_.stop(); log(q(e.what()),true); }
        });
        remote_=std::make_unique<hive::reporting::DiscordRemote>(remote_commands());
        remote_->retire_commands({"actions","action","set","enable","disable","order","modules","module","focus","buffs",
            "royal-jelly","ssa","blender-restart","shrine-restart","planter-check","planter-timers"});
        extensions_->commands_changed=[this] {
            try { remote_->set_commands(remote_commands()); }
            catch(const std::exception& e) { log("Could not update Discord commands: "+q(e.what())); }
        };
        status_page_->add_remote(new hive::app::RemoteSettingsPage(settings_,*remote_,nullptr,status_page_->discord_user(),[this](const hive::reporting::RemoteSettings& value) {
            status_page_->use_bot(value.enabled?std::optional(hive::reporting::BotDestination{value.token,value.channel}):std::nullopt);
        }));
        connect(&start_timer_,&QTimer::timeout,this,[this] {
            if(countdown_>0 && --countdown_>0) return;
            try {
                if (!initial_join_) {
                    const auto previous=selected();
                    refresh();
                    if (selected() && selected()!=previous) {
                        const auto window=desktop_->inspect(selected()).window;
                        if (!desktop_->focus_client(window)) throw std::runtime_error("Could not focus the Roblox window");
                        if (++focus_attempts_>8) throw std::runtime_error("Roblox did not come to the foreground");
                        return;
                    }
                    if (!selected()) initial_join_.emplace(session_.connect_client(hive::monotonic_now()));
                }
                if(initial_join_) {
                    const auto check=desktop_->inspect(selected());
                    if(check.escape_down) throw std::runtime_error("Emergency stop");
                    if(!check.input_allowed || !desktop_->permissions().capture) throw std::runtime_error("Game permissions are required before joining");
                    const auto window=initial_join_->update(*desktop_,hive::monotonic_now());
                    if(!window) return;
                    window_id_=window->id; initial_join_.reset();
                }
                auto snapshot=desktop_->inspect(selected());
                if (!snapshot.input_allowed) throw std::runtime_error("Input permission is required before starting");
                if (!snapshot.focused) {
                    if (!snapshot.window.id || !desktop_->focus_client(snapshot.window) || ++focus_attempts_>8)
                        throw std::runtime_error("Roblox did not become the active application before starting");
                    return;
                }
                start_timer_.stop(); focus_attempts_=0;
                if (pending_feature_) { auto launch=std::move(*pending_feature_); pending_feature_.reset(); session_.start_feature(snapshot.window,std::move(launch)); }
                else if (routine_) { if (resuming_) routine_->resume(); else routine_->start(snapshot.window); }
                else if (resuming_) session_.resume();
                else throw std::logic_error("No session work was prepared");
            } catch (const std::exception& e) { start_timer_.stop(); initial_join_.reset(); routine_.reset(); pending_feature_.reset(); launch_error_=q(e.what()); log(launch_error_,true); }
        });
        auto timer=new QTimer(this); timer->setInterval(150);
        connect(timer,&QTimer::timeout,this,[this,stop,run_routine,start_macro,enabled_features,player_speed,equipment,existing_hive] {
            const auto snapshot=session_.snapshot();
            follow_window(snapshot);
            if (snapshot.feature && hive::automation::can_handoff(*snapshot.feature)) {
                const auto name=extensions_->feature_name(snapshot.feature->module_id);
                const bool skipped=snapshot.feature->state!=hive::automation::State::completed;
                const auto outcome=name+(skipped?" skipped · ":" · ")+q(snapshot.feature->message);
                if (name!=q(snapshot.feature->module_id) && outcome!=last_outcome_) {
                    log(outcome);
                    if (snapshot.feature->state==hive::automation::State::failed) discord_->error(outcome);
                    else if (skipped) discord_->notice(outcome);
                    last_outcome_=outcome;
                }
            }
            hive::extensions::SessionView view{snapshot.status,snapshot.now,snapshot.generation,snapshot.feature,snapshot.pause_requests,{},snapshot.session_id};
            bool routine_failed=false;
            if (routine_ && !start_timer_.isActive()) {
                view=routine_->tick(); extensions_->render();
                const auto loop_state=routine_->state();
                if (loop_state==hive::extensions::RoutineState::failed || loop_state==hive::extensions::RoutineState::stopped) {
                    routine_failed=loop_state==hive::extensions::RoutineState::failed;
                    routine_.reset();
                }
            } else extensions_->update(view);
            if (const auto hive=hive_setup_->read(); hive && hive->claimed_slot
                && hive->context.window.id==snapshot.window.id && hive->context.generation==snapshot.generation
                && existing_hive->currentData().toUInt()!=*hive->claimed_slot) {
                const QSignalBlocker block(existing_hive);
                existing_hive->setCurrentIndex(existing_hive->findData(static_cast<int>(*hive->claimed_slot)));
                settings_.setValue("game/existingHive",static_cast<int>(*hive->claimed_slot));
            }
            const auto& status=view.status;
            QString activity="Other";
            if (view.feature) switch (view.feature->activity) {
            case hive::WorkActivity::gathering: activity="Gathering"; break;
            case hive::WorkActivity::travelling: activity="Travelling"; break;
            case hive::WorkActivity::converting: activity="Converting"; break;
            case hive::WorkActivity::recovering: activity="Recovering"; break;
            case hive::WorkActivity::other: break;
            }
            if ((discord_->settings().enabled || remote_->settings().enabled) && status.state==hive::RunState::running && reporting_nectar_) {
                const auto& selected=discord_->settings().buff_ids;
                for (const auto& source:{reporting_buffs_,reporting_upkeep_}) if (source) {
                    const bool needed=source==reporting_buffs_?std::any_of(selected.begin(),selected.end(),[](const auto& id) {
                            return id=="buff/Haste" || id=="buff/Haste+" || id=="buff/Oil" || id=="buff/Smoothie" || id=="buff/Bear morph";
                        }):
                        std::any_of(hive::game::tracked_buffs().begin(),hive::game::tracked_buffs().end(),[&](const auto& type) {
                            return selected.contains("buff/"+QString::fromUtf8(type.name.data(),qsizetype(type.name.size())));
                        });
                    if (!needed) continue;
                    const auto reading=source->peek();
                    if (!reading || reading->context.generation!=snapshot.generation || snapshot.now-reading->context.now>hive::Time(source==reporting_buffs_?3000:6000)) source->read();
                }
                const auto nectar=reporting_nectar_->peek();
                if (!nectar || nectar->context.generation!=snapshot.generation || snapshot.now-nectar->context.now>hive::Time(3000)) reporting_nectar_->read();
            }
            const auto current_activity=view.feature?q(view.feature->activity_label):QString{};
            const auto reason=view.feature && view.feature->state==hive::automation::State::failed?q(view.feature->message):q(status.reason);
            const auto failure=current_activity.isEmpty()?reason:current_activity+"\n"+reason;
            discord_->set_history_requested(remote_ && remote_->settings().enabled);
            discord_->update(status.state,view.session_id,failure,activity);
            if (routine_failed || (status.state!=hive::RunState::running && view.feature && view.feature->state==hive::automation::State::failed && !hive::automation::can_handoff(*view.feature))) {
                if (failure!=last_failure_) { discord_->error(failure); last_failure_=failure; }
            } else last_failure_.clear();
            const auto state_key=QString::number(static_cast<int>(status.state));
            if (state_key!=last_status_) {
                if (status.state==hive::RunState::running) { log("Macro started"); discord_->activity("Macro started"); }
                else if (status.state!=hive::RunState::idle) {
                    const auto message=state_name(status.state)+(current_activity.isEmpty()?QString{}:" · "+current_activity)+(reason.isEmpty()?QString{}:" — "+reason);
                    log(message);
                    if (last_failure_.isEmpty() && status.state!=hive::RunState::faulted) discord_->activity(message);
                }
                last_status_=state_key;
                if (status.state!=hive::RunState::running) { status_page_->clear_activity(); seen_observation_errors_.clear(); }
            }
            for (const auto& event:session_.take_activity_events()) {
                const auto& reporting=discord_->settings();
                const bool photo=event.image_region.has_value() || (event.status.kind==hive::ActivityKind::failure && reporting.errors && reporting.error_screenshots);
                status_page_->event(event.status,reporting.enabled && photo?hive::app::game_image(event.frame,event.image_region):QImage{});
            }
            if (status.state==hive::RunState::running && view.feature && view.feature->state==hive::automation::State::running) {
                last_outcome_.clear();
                status_page_->activity(view.feature->status);
            }
            if (status.state==hive::RunState::running) {
                const auto reading=session_.observations();
                const auto error=q(reading.error);
                if (!error.isEmpty() && seen_observation_errors_.insert(error).second) log("Game reading: "+error);
            }
            auto state_text=state_name(status.state);
            auto action_text=QString(!status.reason.empty()?q(status.reason):status.job.empty()?"Enable your features, then start the macro.":q(status.job)+" · "+q(status.action));
            if (const auto& feature=view.feature) {
                const auto label=current_activity.isEmpty()?extensions_->feature_name(feature->module_id):current_activity;
                action_text=feature->state==hive::automation::State::running?label:
                    label+(feature->message.empty()?QString{}:"\n"+q(feature->message));
            }
            if (routine_ && status.state==hive::RunState::running && (!view.feature || hive::automation::can_handoff(view.feature->state))) action_text="Waiting for the next activity";
            if (routine_ && routine_->state()==hive::extensions::RoutineState::pausing) action_text="Will pause when the current activity finishes. Stop / Esc ends it immediately.";
            if (!launch_error_.isEmpty()) { state_text="Could not start"; action_text=launch_error_; }
            if (start_timer_.isActive()) {
                state_text=initial_join_ && countdown_==0?"Joining Bee Swarm":QString("Starting in %1…").arg(countdown_);
                action_text=initial_join_?q(initial_join_->status()):"Preparing Roblox.";
            }
            const bool active=status.state==hive::RunState::running || status.state==hive::RunState::paused;
            const auto features=extensions_->enabled_features();
            enabled_features->setText(features.empty()?"Enable a module to get started.":features.join(" · "));
            if (!active && !start_timer_.isActive() && launch_error_.isEmpty() && status.state==hive::RunState::idle) {
                state_text=!permissions_ready_?"Permissions needed":features.empty()?"Choose your features":"Ready";
                action_text=!permissions_ready_?"Allow screen capture and input in Settings.":!selected()?"Start macro will open Bee Swarm using your server settings.":features.empty()?"Choose what to run on the Modules page.":"Start macro runs your enabled features together.";
            }
            state_->setText(state_text); action_->setText(action_text); action_->setToolTip(action_text);
            equipment->setEnabled(!active && !start_timer_.isActive());
            existing_hive->setEnabled(!active && !start_timer_.isActive());
            player_speed->set_available(!active && !start_timer_.isActive());
            run_routine->setEnabled(!features.empty() && !active && !start_timer_.isActive());
            start_macro->setEnabled(run_routine->isEnabled());
            pause_->setEnabled(active && !start_timer_.isActive()); stop->setEnabled(active || start_timer_.isActive());
            pause_->setToolTip(routine_?"Use F2 while Roblox is focused to pause after the current activity, cancel a pending pause, or resume. Esc stops immediately.":"Pause the current work.");
            pause_->setText(status.state==hive::RunState::paused?"Resume":routine_?(routine_->state()==hive::extensions::RoutineState::pausing?"Keep running":"Pause after activity"):"Pause");
        }); timer->start();
        auto connection_timer=new QTimer(this); connection_timer->setInterval(2000);
        connect(connection_timer,&QTimer::timeout,this,[this] {
            const auto status=session_.status().state;
            if (status!=hive::RunState::running && status!=hive::RunState::paused && !start_timer_.isActive()) refresh();
        }); connection_timer->start(); refresh();
    }
    ~Window() override { start_timer_.stop(); session_.stop(); }
};
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QCoreApplication::setApplicationVersion(AUREX_VERSION);
    app.setApplicationDisplayName("Aurex");
#ifndef Q_OS_MACOS
    app.setWindowIcon(QIcon(":/branding/aurex.png"));
#endif
    app.setApplicationName("Aurex"); app.setOrganizationName("Aurex");
    hive::apply_theme(app);
    try {
        Window window(hive::app::prepare_local_data()); window.show();
        const auto args=app.arguments();
        if (args.contains("--screenshot")) {
            const auto index=args.indexOf("--screenshot"); if (index+1>=args.size()) return 1;
            QTimer::singleShot(300,[&,path=args[index+1]] { app.exit(window.grab().save(path)?0:1); });
        }
        return app.exec();
    } catch (const std::exception& error) {
        QMessageBox::critical(nullptr,"Aurex could not open",QString::fromUtf8(error.what()));
        return 1;
    }
}
