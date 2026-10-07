include(FetchContent)
set(UPNPC_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(UPNPC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(UPNPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
set(UPNPC_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(miniupnp
  URL https://github.com/miniupnp/miniupnp/archive/bf4215a7574f88aa55859db9db00e3ae58cf42d6.tar.gz
  URL_HASH SHA256=213a37a83f1b77a3c4f90b4dad70a5d2460d1b1042d3b0b4aef20234ae809add
  SOURCE_SUBDIR miniupnpc
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(natpmp
  URL https://github.com/miniupnp/libnatpmp/archive/134fc89e2781e154e40042641f4d8bcbe42579f1.tar.gz
  URL_HASH SHA256=be1864ed4e54a122590fd2de3824b085386b6e4aa9a91738c277c280ab8bf188
  SOURCE_SUBDIR library-only
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(miniupnp natpmp)
add_library(hive_natpmp STATIC "${natpmp_SOURCE_DIR}/natpmp.c" "${natpmp_SOURCE_DIR}/getgateway.c")
file(MAKE_DIRECTORY "${natpmp_BINARY_DIR}/include")
foreach(header natpmp.h natpmp_declspec.h)
  configure_file("${natpmp_SOURCE_DIR}/${header}" "${natpmp_BINARY_DIR}/include/${header}" COPYONLY)
endforeach()
target_include_directories(hive_natpmp PUBLIC "${natpmp_BINARY_DIR}/include")
target_compile_definitions(hive_natpmp PRIVATE NATPMP_MAX_RETRIES=3)
if(WIN32)
  target_sources(hive_natpmp PRIVATE "${natpmp_SOURCE_DIR}/wingettimeofday.c")
  target_compile_definitions(hive_natpmp PUBLIC NATPMP_STATICLIB PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
  target_link_libraries(hive_natpmp PUBLIC ws2_32 iphlpapi)
endif()
