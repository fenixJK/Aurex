add_library(hive_accounts module.cpp controller.cpp)
target_link_libraries(hive_accounts PUBLIC hive_extensions hive_account_connection)
add_library(hive_accounts_ui page.cpp)
target_link_libraries(hive_accounts_ui PUBLIC hive_accounts Qt6::Widgets)
