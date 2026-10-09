#pragma once
#include "controller.hpp"
#include <QWidget>
class QLabel;
class QPushButton;
namespace hive::features::accounts {
class Page final : public QWidget {
public:
    explicit Page(std::shared_ptr<Controller> controller,QWidget* parent=nullptr);
private:
    std::shared_ptr<Controller> controller_;
    QLabel *status_{},*members_{};
    QPushButton *host_{},*join_{},*leave_{},*copy_{},*retry_{};
    void refresh();
    void setup(bool host);
};
}
