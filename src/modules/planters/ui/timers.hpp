#pragma once
#include "modules/planters/account.hpp"
#include <QWidget>

namespace hive::features::planters {
QWidget* planter_timers(std::shared_ptr<Account> account,QWidget* parent);
}
