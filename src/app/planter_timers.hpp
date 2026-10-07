#pragma once
#include "hive/modules/planters/execution.hpp"
#include <QWidget>

namespace hive::app {
QWidget* planter_timers(std::shared_ptr<features::planters::Account> account,QWidget* parent);
}
