add_library(hive_reporting discord.cpp remote.cpp remote_status.cpp report.cpp history.cpp icons.cpp)
target_link_libraries(hive_reporting PUBLIC hive_core Qt6::Gui Qt6::Network Qt6::WebSockets PRIVATE Qt6::Concurrent)

qt_add_resources(HIVE_REPORT_ICON_SOURCES report_icons.qrc)
target_sources(hive_reporting PRIVATE ${HIVE_REPORT_ICON_SOURCES})
