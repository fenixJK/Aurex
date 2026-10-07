#pragma once
#include "hive/module_host/presentation.hpp"
#include <QWidget>
namespace hive {
class ModulePlot final : public QWidget {
    std::shared_ptr<const extensions::Plot> plot_;
    void paintEvent(QPaintEvent*) override;
public:
    explicit ModulePlot(QWidget* parent=nullptr):QWidget(parent) { setMinimumHeight(440); setObjectName("module-plot"); }
    void setPlot(std::shared_ptr<const extensions::Plot> plot);
    bool hasData() const { return bool(plot_); }
    bool savePng(const QString& path);
    bool saveCsv(const QString& path);
};
}
