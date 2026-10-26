add_library(hive_vichop hop.cpp module.cpp)
target_link_libraries(hive_vichop PUBLIC hive_combat hive_extensions)
add_library(hive_public_servers servers.cpp)
target_link_libraries(hive_public_servers PUBLIC hive_game PRIVATE Qt6::Network)
