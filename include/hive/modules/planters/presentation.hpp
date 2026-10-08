#pragma once
#include "preferences.hpp"
#include "planning.hpp"
#include "hive/module_host/presentation.hpp"
#include <atomic>
namespace hive::features::planters {
std::shared_ptr<const extensions::Plot> simulation_plot(Preferences preferences,std::size_t capacity,unsigned days,
    std::shared_ptr<const std::atomic_bool> cancelled);
std::shared_ptr<const extensions::Plot> forecast_plot(PlanningSnapshot snapshot,unsigned hours);
}
