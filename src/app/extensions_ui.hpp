#pragma once
#include "core/module_host/routine_run.hpp"
#include <QObject>
#include <functional>

class QStackedWidget;
class QWidget;
class QVBoxLayout;
class QLabel;
class QPushButton;
class QTabBar;
class QTimer;
namespace hive {
class Navigation;
class ExtensionsUi final : public QObject {
public:
    using RegisterProviders = std::function<void(extensions::Host&)>;
    using TargetPicker = std::function<std::optional<WindowPoint>(const std::string&)>;
    using ToolFactory = std::function<QWidget*(const std::string&,QWidget*)>;
    using PageFactory = std::function<QWidget*(const std::string&,QWidget*)>;
    ExtensionsUi(Navigation* navigation,QStackedWidget* pages,QString bundled,QString user_root,QString state_file,std::function<bool()> busy,RegisterProviders register_providers,extensions::Host::FailureHandler failure_handler={},ToolFactory tool_factory={},std::function<void(automation::Launch)> launch={},TargetPicker picker={},PageFactory page_factory={});
    ~ExtensionsUi() override;
    void update(const extensions::SessionView& view);
    void render();
    void replace_providers(const RegisterProviders& registration);
    QStringList enabled_features() const;
    std::vector<extensions::ModuleReport> module_reports() const;
    QString feature_name(const std::string& id) const;
    void apply_settings();
    const std::map<std::string,extensions::Entry>& entries() const { return host_.entries(); }
    extensions::Settings module_settings(const std::string& id) const;
    void configure_module(const std::string& id,const extensions::Settings& values);
    void enable_module(const std::string& id,bool enabled);
    extensions::CommandResult invoke_module(const std::string& id,const std::string& command,bool remote=false,extensions::CommandInput input={});
    const extensions::CommandRegistry& commands() const { return host_.commands(); }
    std::function<void()> commands_changed;
    std::vector<std::string> run_order() const;
    void set_run_order(std::vector<std::string> order);
    std::unique_ptr<extensions::RoutineRun> routine(Session& session,std::unique_ptr<automation::ActivityPolicy> policy,std::optional<automation::Launch> preparation={}) {
        apply_settings();
        return std::make_unique<extensions::RoutineRun>(host_,session,std::move(policy),std::move(preparation),order_);
    }
private:
    extensions::Host host_;
    std::uint64_t command_revision_{};
    Navigation* navigation_;
    QStackedWidget* pages_;
    QStringList roots_;
    QString user_root_;
    std::function<bool()> busy_;
    ToolFactory tool_factory_;
    PageFactory page_factory_;
    std::function<void(automation::Launch)> launch_;
    TargetPicker picker_;
    QString order_file_;
    std::vector<std::string> order_;
    QTabBar* order_tabs_{};
    QWidget* manager_{};
    QVBoxLayout* list_{};
    QLabel* message_{};
    QPushButton *reload_{}, *install_{};
    QTimer* memory_cleanup_{};
    struct Page;
    std::map<std::string,std::unique_ptr<Page>> extension_pages_;
    void scan();
    void reconcile();
    void show_page();
    void release_hidden_pages();
    bool release_page(const std::string& id,bool save=true);
    void build_page(const std::string& id);
    void render_page(const std::string& id);
    void refresh_list();
    bool perform(const std::function<void()>& action);
};
}
