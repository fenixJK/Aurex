#include "player_speed.hpp"
#include <QSettings>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <cmath>

namespace hive::app {
namespace {
bool valid(double speed) { return std::isfinite(speed) && speed>=8 && speed<=200; }
constexpr auto key="player/baseMovementSpeed";
class SpeedInput final : public QDoubleSpinBox {
    QString textFromValue(double value) const override {
        if (value==0) return {};
        auto text=QDoubleSpinBox::textFromValue(value);
        const auto decimal=locale().decimalPoint();
        if(text.contains(decimal)) {
            while(text.endsWith(locale().zeroDigit())) text.chop(locale().zeroDigit().size());
            if(text.endsWith(decimal)) text.chop(decimal.size());
        }
        return text;
    }
public:
    SpeedInput() { lineEdit()->setPlaceholderText("Not set"); }
};
}
PlayerSpeed::PlayerSpeed(QSettings& settings,QWidget* parent):QWidget(parent),settings_(settings) {
    auto layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
    auto row=new QHBoxLayout;
    row->addWidget(new QLabel("Base move speed"));
    base_=new SpeedInput; base_->setObjectName("base-move-speed");
    base_->setDecimals(3); base_->setRange(0,200); base_->setKeyboardTracking(false);
    base_->setToolTip("Your displayed Movespeed without temporary speed buffs, including permanent equipment bonuses. Update it after changing equipment or hive bonuses.");
    bool ok=false; const auto saved=settings_.value(key).toDouble(&ok);
    if (ok && valid(saved)) base_->setValue(saved);
    base_->setFixedWidth(100); row->addWidget(base_);
    row->addStretch(); layout->addLayout(row);
    connect(base_,&QDoubleSpinBox::valueChanged,this,[this](double value) {
        if (busy_ || (on_changed && !on_changed())) {
            bool ok=false; const auto saved=settings_.value(key).toDouble(&ok);
            QSignalBlocker blocked(base_); base_->setValue(ok && valid(saved)?saved:0);
            return;
        }
        if (valid(value)) settings_.setValue(key,value); else settings_.remove(key);
    });

}
std::optional<double> PlayerSpeed::base_speed() const {
    return valid(base_->value())?std::optional{base_->value()}:std::nullopt;
}
void PlayerSpeed::set_available(bool idle) {
    busy_=!idle; base_->setEnabled(idle);
}
}
