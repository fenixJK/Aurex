add_library(hive_session
  session.cpp
  observer.cpp
  recognition.cpp
  recovery.cpp
  client_rejoin.cpp)
target_link_libraries(hive_session PUBLIC hive_automation Threads::Threads PRIVATE ${OpenCV_LIBS})
target_include_directories(hive_session SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
