add_library(hive_session
  session.cpp
  observer.cpp
  recognition.cpp
  recovery.cpp
  client_rejoin.cpp)
target_link_libraries(hive_session PUBLIC hive_automation Threads::Threads)
