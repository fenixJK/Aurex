#pragma once
#include <QWidget>
#include <functional>
#include <optional>

class QSettings;
class QDoubleSpinBox;

namespace hive::app {
class PlayerSpeed final : public QWidget {
    QSettings& settings_;
    QDoubleSpinBox* base_{};
    bool busy_{};
public:
    explicit PlayerSpeed(QSettings& settings,QWidget* parent=nullptr);
    std::optional<double> base_speed() const;
    void set_available(bool idle);
    std::function<bool()> on_changed;
};
}
