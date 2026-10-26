add_library(hive_combat
  plan.cpp
  catalog.cpp
  encounter.cpp
  hunt.cpp
  progress.cpp
  preparation.cpp)
target_link_libraries(hive_combat PUBLIC hive_automation)
add_library(hive_combat_module module.cpp server_visit.cpp)
target_link_libraries(hive_combat_module PUBLIC hive_combat hive_visit_module)

if(HIVE_BUILD_DESKTOP)
  add_subdirectory(vichop)
endif()
