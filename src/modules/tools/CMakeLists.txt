add_library(hive_tools results.cpp mutations.cpp rolling.cpp bitterberries.cpp module.cpp)
target_link_libraries(hive_tools PUBLIC hive_automation hive_extensions)
