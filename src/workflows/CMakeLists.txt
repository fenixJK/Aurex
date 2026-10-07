add_library(hive_workflows
  dialog.cpp
  inventory.cpp
  sprinklers.cpp
  conversion.cpp
  interaction.cpp
  travel.cpp
  checkpoint.cpp
  navigation.cpp)
target_link_libraries(hive_workflows PUBLIC hive_game)
