#pragma once
#include "core/runtime/model.hpp"
#include <QString>
class QWidget;
namespace hive::app {
std::optional<WindowPoint> pick_window_point(const Frame& frame,const QString& prompt,QWidget* parent);
}
