#pragma once
#include "core/module_host/presentation.hpp"
#include <QWidget>
namespace hive {
QImage plot_image(const extensions::Plot& plot,QSize size={1000,620});
class ModulePlot final : public QWidget {
    std::shared_ptr<const extensions::Plot> plot_;
public:
    explicit ModulePlot(QWidget* parent=nullptr);
    void setPlot(std::shared_ptr<const extensions::Plot> plot);
    bool hasData() const { return bool(plot_); }
    bool savePng(const QString& path);
    bool saveCsv(const QString& path);
};
}
