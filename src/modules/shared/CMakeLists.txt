add_library(hive_policies policies.cpp)
target_link_libraries(hive_policies PUBLIC hive_game)
add_library(hive_visit_module visit_module.cpp visit_cooldowns.cpp)
target_link_libraries(hive_visit_module PUBLIC hive_automation)

if(HIVE_BUILD_DESKTOP)
  add_library(hive_visit_cooldown_store visit_cooldown_store.cpp)
  target_link_libraries(hive_visit_cooldown_store PUBLIC hive_visit_module PRIVATE Qt6::Core)
endif()
