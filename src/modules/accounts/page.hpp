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
    QLabel *status_{},*boost_{},*role_{},*role_help_{};
    QWidget *choose_role_{},*connected_{};
    QTabWidget* tabs_{};
    coordination::Role shown_role_{coordination::Role::offline};
    QTableWidget* members_{};
    QPushButton *host_{},*join_{},*leave_{},*copy_{},*retry_{};
    void refresh();
    void fit_tabs();
    void setup(bool host);
};
}
