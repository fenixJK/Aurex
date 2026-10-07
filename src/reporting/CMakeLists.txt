add_library(hive_reporting discord.cpp remote.cpp remote_status.cpp report.cpp history.cpp)
target_link_libraries(hive_reporting PUBLIC hive_core Qt6::Gui Qt6::Network Qt6::WebSockets PRIVATE Qt6::Concurrent)
