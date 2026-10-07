add_library(hive_quests tracker.cpp catalog.cpp)
target_link_libraries(hive_quests PUBLIC hive_game hive_module_api)
add_library(hive_quest_module
  module.cpp
  detection.cpp
  layout.cpp
  browser.cpp
  menu.cpp
  cycle.cpp
  actions.cpp)
target_link_libraries(hive_quest_module PUBLIC hive_quests hive_automation)
