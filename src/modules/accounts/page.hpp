#pragma once
#include "controller.hpp"
#include <QWidget>
class QLabel;
class QPushButton;
class QTableWidget;
class QTabWidget;
namespace hive { class ChoiceControl; }
namespace hive::features::accounts {
class Page final : public QWidget {
public:
    explicit Page(std::shared_ptr<Controller> controller,QWidget* parent=nullptr,
        const std::map<std::string,QWidget*>& sections={});
private:
    std::shared_ptr<Controller> controller_;
    enum class Setup { role,alt_type,settings };
    Setup setup_{Setup::role},type_back_{Setup::role};
    QLabel *status_{},*boost_{},*role_{},*role_help_{};
    QWidget *choose_role_{},*choose_type_{},*header_{};
    ChoiceControl* alt_type_{};
    QTabWidget* tabs_{};
    coordination::Role shown_role_{coordination::Role::offline};
    QTableWidget* members_{};
    QPushButton *host_{},*alt_{},*join_{},*leave_{},*copy_{},*retry_{},*back_{},*change_type_{},*support_{},*guiding_{};
    void refresh();
    void fit_tabs();
    void select_type(const std::string& type);
    void setup(bool host);
};
}
