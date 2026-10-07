include(FetchContent)
FetchContent_Declare(lua
  URL https://www.lua.org/ftp/lua-5.4.9.tar.gz
  URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(lua)
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${lua_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM LUA_SOURCES "${lua_SOURCE_DIR}/src/lua.c" "${lua_SOURCE_DIR}/src/luac.c")
add_library(hive_lua STATIC ${LUA_SOURCES})
target_include_directories(hive_lua PUBLIC "${lua_SOURCE_DIR}/src")
set_target_properties(hive_lua PROPERTIES C_STANDARD 99)
if(UNIX)
  target_link_libraries(hive_lua PUBLIC m)
endif()
