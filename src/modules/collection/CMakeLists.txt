add_library(hive_collection
  plan.cpp
  games.cpp
  card_memory.cpp
  items.cpp
  special.cpp
  orders.cpp)
target_link_libraries(hive_collection PUBLIC hive_automation)
add_library(hive_collection_module module.cpp)
target_link_libraries(hive_collection_module PUBLIC hive_collection hive_visit_module)

if(HIVE_BUILD_DESKTOP)
  add_library(hive_collection_store orders_store.cpp)
  target_link_libraries(hive_collection_store PUBLIC hive_collection PRIVATE Qt6::Core)
endif()
