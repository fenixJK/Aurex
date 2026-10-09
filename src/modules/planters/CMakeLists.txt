add_library(hive_planters
  preferences.cpp
  planner.cpp
  catalog.cpp
  simulation.cpp
  controller.cpp
  planning.cpp)
target_link_libraries(hive_planters PUBLIC hive_core)
add_library(hive_planter_execution
  execution.cpp
  actions.cpp
  observations.cpp
  travel.cpp)
target_link_libraries(hive_planter_execution PUBLIC hive_planters hive_automation)
add_library(hive_planter_module module.cpp presentation.cpp)
target_link_libraries(hive_planter_module PUBLIC hive_planter_execution)

if(HIVE_BUILD_DESKTOP)
  add_library(hive_planter_store store.cpp)
  target_link_libraries(hive_planter_store PUBLIC hive_planters PRIVATE Qt6::Core)
  add_library(hive_planters_ui ui/timers.cpp)
  target_link_libraries(hive_planters_ui PUBLIC hive_planter_execution Qt6::Widgets)
endif()
