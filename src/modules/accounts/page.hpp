#pragma once
#include "controller.hpp"
#include <QWidget>
class QLabel;
class QPushButton;
class QTableWidget;
class QTabWidget;
namespace hive::features::accounts {
class Page final : public QWidget {
public:
    explicit Page(std::shared_ptr<Controller> controller,QWidget* parent=nullptr,
        const std::map<std::string,QWidget*>& sections={});
private:
    std::shared_ptr<Controller> controller_;
    enum class Setup { role,settings };
    Setup setup_{Setup::role};
    QLabel *status_{},*boost_{},*role_{},*role_help_{};
    QWidget *choose_role_{},*header_{};
    QTabWidget* tabs_{};
    coordination::Role shown_role_{coordination::Role::offline};
    QTableWidget* members_{};
    QPushButton *host_{},*alt_{},*join_{},*leave_{},*copy_{},*retry_{},*back_{};
    void refresh();
    void fit_tabs();
    void setup(bool host);
};
}
