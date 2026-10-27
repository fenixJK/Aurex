#pragma once
#include "core/runtime/model.hpp"
#include <QString>
class QWidget;
namespace hive { class Desktop; }
namespace hive::app {
std::optional<WindowPoint> pick_window_point(const Frame& frame,const QString& prompt,QWidget* parent);
std::optional<WindowPoint> pick_window_point(Desktop& desktop,const Window& window,const QString& prompt,QWidget* parent);
}
