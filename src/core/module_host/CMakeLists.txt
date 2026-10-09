add_library(hive_module_api requests.cpp request_channels.cpp commands.cpp activity_policy.cpp)
target_link_libraries(hive_module_api PUBLIC hive_game)

if(HIVE_BUILD_DESKTOP)
  add_library(hive_extensions
    routine_run.cpp
    routine.cpp
    module.cpp
    settings_text.cpp
    manifest.cpp
    host.cpp
    package.cpp)
  target_link_libraries(hive_extensions PUBLIC hive_automation hive_session Qt6::Core)
  add_library(hive_extension_providers dashboard.cpp script_module.cpp script_contributor.cpp)
  target_link_libraries(hive_extension_providers PUBLIC hive_extensions PRIVATE hive_scripts)
endif()
